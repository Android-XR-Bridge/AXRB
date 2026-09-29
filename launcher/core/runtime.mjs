import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import { installFiles } from './game-files.mjs';
import { bridgeEnvironment } from './quest-bridge.mjs';
import { selectGuestClock } from './clock-policy.mjs';
import { describePermissionFailure, parsePermissionPrompt, parseRuntimePermissions, validPermission } from './permissions.mjs';

// Package-manager queries wait behind background compilation after an
// install, which is measured in minutes rather than seconds.
const PACKAGE_QUERY_TIMEOUT = 180000;
const PACKAGE_QUERY_TIMEOUT_MESSAGE = 'Android did not answer a package query in time.';

export function run(executable, args, { timeout = 120000, onOutput = () => {}, signal, requireCompleteOutput = false, rejectStderr = false, timeoutMessage = 'Operation timed out. Check the Android runtime and try again.' } = {}) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) { reject(new Error('Cancelled')); return; }
    const child = spawn(executable, args, { windowsHide: true, shell: false });
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    const stdout = { stream: `${child.pid}:stdout` }, stderr = { stream: `${child.pid}:stderr` };
    let output = '', errors = '', settled = false, truncated = false, drain = null;
    const timer = setTimeout(() => { child.kill(); finish(new Error(timeoutMessage)); }, timeout);
    const abort = () => { child.kill(); };
    signal?.addEventListener('abort', abort, { once: true });
    function finish(error) { if (settled) return; settled = true; clearTimeout(timer); clearTimeout(drain); signal?.removeEventListener('abort', abort); child.stdout.destroy(); child.stderr.destroy(); error ? reject(error) : resolve(output); }
    function complete(code) { finish(signal?.aborted ? new Error('Cancelled') : requireCompleteOutput && truncated ? new Error('Device file list exceeds the supported size; no incomplete import was saved.') : code === 0 && !(rejectStderr && errors.trim()) ? null : new Error((errors || output || `Process exited with code ${code}`).slice(-3000))); }
    child.stdout.on('data', b => { const next = output + b.toString(); truncated ||= next.length > 8 * 1024 * 1024; output = next.slice(-8 * 1024 * 1024); onOutput(b.toString(), stdout); });
    child.stderr.on('data', b => { errors = (errors + b.toString()).slice(-16384); onOutput(b.toString(), stderr); });
    child.stdout.once('close', () => onOutput('', { ...stdout, end: true }));
    child.stderr.once('close', () => onOutput('', { ...stderr, end: true }));
    child.stdout.on('error', () => {}); child.stderr.on('error', () => {});
    child.on('error', e => finish(new Error(`Could not start ${path.basename(executable)}: ${e.code || 'unknown error'}`)));
    // `close` waits for every writer on the stdio pipes to let go. A process we
    // start can leave one behind: the Android emulator inherits the launching
    // script's pipes and outlives it by hours, so `close` would never arrive.
    // Settle on the exit code, giving the pipes a moment to deliver a last chunk.
    child.on('exit', code => { drain = setTimeout(() => complete(code), 500); });
    child.on('close', code => complete(code));
  });
}
// Every PowerShell call runs a script file with -File and passes each value as
// its own argv entry, which the parameter binder takes literally. That removes
// the quoting problem this used to solve by building a command string, and it
// keeps the launcher from spawning base64 command lines that behavioural
// antivirus engines score as obfuscation. The scripts set the console encoding
// and reduce a terminating error to its message, which the wrapper once did.
export function powershellArgs(script, parameters) {
  const args = ['-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-ExecutionPolicy', 'Bypass', '-File', script];
  for (const [key, value] of Object.entries(parameters)) {
    if (!/^[a-zA-Z]+$/.test(key)) throw new Error('Invalid PowerShell parameter.');
    // -File has no way to write "-Switch:$false": a switch is bound by presence.
    if (typeof value === 'boolean') { if (value) args.push(`-${key}`); continue; }
    const text = String(value);
    // Separate argv entries stop a value from splitting into further arguments,
    // but a value that is itself a parameter name still binds as one.
    if (text.startsWith('-')) throw new Error(`PowerShell parameter ${key} cannot start with "-".`);
    args.push(`-${key}`, text);
  }
  return args;
}
export function windowsFeaturesCommand(root, windowsDir = process.env.WINDIR || 'C:\\Windows') {
  return powershellArgs(path.join(root, 'scripts/run/open_windows_features.ps1'), { WindowsDir: windowsDir });
}
export async function openWindowsFeatures(root, execute = run) {
  return execute('powershell.exe', windowsFeaturesCommand(root), { timeout: 15000 });
}
export function validPackage(value) {
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(value || '')) throw new Error('Invalid Android package name.');
  return value;
}
// Play reads identity from the live Android build, never from the library's
// local APK metadata, so an externally updated install cannot inherit a stale
// profile. `dumpsys package` also prints stale hidden-system records, so the
// read is scoped to the current Packages section and the exact
// `Package [<name>]` header, and the owner user must show installed=true.
// versionCode stays an exact string; a missing versionName becomes '' instead
// of borrowing the library's copy.
function parseInstalledIdentity(output, packageName) {
  const escaped = packageName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const header = new RegExp(`^\\s*Package \\[${escaped}\\](?:\\s*\\([^)]*\\))?\\s*:\\s*$`);
  const lines = String(output ?? '').split(/\r?\n/);
  let scoped = false;
  const section = [];
  for (const line of lines) {
    if (/^\s*Packages:\s*$/.test(line)) { scoped = true; continue; }
    if (/^\S/.test(line)) { scoped = false; continue; }
    if (scoped) section.push(line);
  }
  const matches = section.filter(line => header.test(line));
  if (matches.length === 0) throw new Error(`${packageName} is not installed. Refresh your library.`);
  if (matches.length > 1) throw new Error(`Multiple installed records for ${packageName}. Refresh your library.`);
  const start = section.findIndex(line => header.test(line));
  const block = [];
  for (let i = start + 1; i < section.length && !/^\s*Package \[/.test(section[i]); i++) block.push(section[i]);
  const user = block.find(line => /^\s*User 0:/.test(line)) ?? '';
  if (!/\binstalled=true\b/.test(user)) throw new Error(`${packageName} is not installed. Refresh your library.`);
  const code = /^\s*versionCode=([0-9]+)(?:\s|$)/.exec(block.find(line => /^\s*versionCode=/.test(line)) ?? '')?.[1];
  if (!code) throw new Error(`Could not read the installed version for ${packageName}. Refresh your library.`);
  const name = /^\s*versionName=(.*)$/.exec(block.find(line => /^\s*versionName=/.test(line)) ?? '')?.[1];
  return { package: packageName, version: name === 'null' ? '' : name ?? '', versionCode: code };
}
export class Runtime {
  constructor(root, settings, onOutput = () => {}) { this.root = root; this.settings = settings; this.onOutput = onOutput; this.child = null; }
  adb(args, options) { return run(path.join(this.settings.sdk, 'platform-tools/adb.exe'), ['-s', `emulator-${this.settings.port}`, ...args], options); }
  // AXRB's private ADB server (port 5038) otherwise outlives the launcher,
  // holding the port and, in portable mode, an adb.exe inside the folder. It
  // stays while a game or the emulator still runs: the game's image stream
  // rides that server's adb reverse, and sessions can outlive the launcher.
  // watchedPhase is the watchdog's latest reading; trusting it spares quit a
  // PowerShell status run of up to 10 seconds. Without one, ask directly.
  async adbServerIdle(watchedPhase) {
    if (this.child || !this.settings.sdk) return false;
    try { await fs.access(path.join(this.settings.sdk, 'platform-tools/adb.exe')); } catch { return false; }
    if (['stopped', 'online', 'starting'].includes(watchedPhase)) return watchedPhase === 'stopped';
    return !(await this.status()).running;
  }
  stopAdbServer() {
    return run(path.join(this.settings.sdk, 'platform-tools/adb.exe'), ['-P', '5038', 'kill-server'], { timeout: 5000 }).catch(() => {});
  }
  async online() { try { return (await this.adb(['get-state'], { timeout: 2500 })).trim() === 'device'; } catch { return false; } }
  // Side-effect-free process + ADB check for the watchdog. Unlike online(),
  // this can tell a genuinely stopped emulator apart from one whose OS process
  // is still alive but unreachable (a stalled boot, a hung shutdown).
  async status() {
    const output = await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/emulator/windows_android_emulator.ps1'), {
      Action: 'Status', Avd: this.settings.avd, Port: this.settings.port, Sdk: this.settings.sdk
    }), { timeout: 10000 });
    const line = output.trim().split(/\r?\n/).filter(Boolean).pop();
    let parsed;
    try { parsed = JSON.parse(line ?? ''); } catch { throw new Error('Could not read the Android emulator process status.'); }
    return {
      running: Boolean(parsed.running),
      pid: Number.isInteger(parsed.processId) ? parsed.processId : null,
      count: Number.isInteger(parsed.count) ? parsed.count : 0,
      adbState: typeof parsed.adbState === 'string' ? parsed.adbState : '',
    };
  }
  async startEmulator({ onOutput, coldBoot = false, recoverUnresponsive = false }) {
    // Allow the script's 180-second graceful shutdown, full boot deadline,
    // and bounded ADB verification retries before timing out its wrapper.
    const guestClock = await selectGuestClock(this.root, this.settings);
    const timeout = (guestClock === 'TscCorrected' ? 40 : 30) * 60 * 1000;
    onOutput?.(`Android clock: ${guestClock}${this.settings.guestClock === 'Auto' || !this.settings.guestClock ? ' (automatic)' : ''}.\n`);
    return run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/emulator/windows_android_emulator.ps1'), {
      Action: 'Start', Avd: this.settings.avd, Port: this.settings.port, Sdk: this.settings.sdk,
      ApiLevel: 36, Abi: 'arm64-v8a', MemoryMB: this.settings.memoryMB, CpuCores: this.settings.cpuCores ?? 4,
      GuestClock: guestClock, GpuSharing: true, ColdBoot: coldBoot, RecoverUnresponsive: recoverUnresponsive,
      ...(this.settings.localApic ? { LocalApic: this.settings.localApic } : {})
    }), { timeout, onOutput });
  }
  async ensure({ onOutput = this.onOutput } = {}) {
    if (await this.online()) {
      let name = '';
      try { name = (await this.adb(['emu', 'avd', 'name'], { timeout: 8000 })).split(/\r?\n/)[0].trim(); } catch { /* Check the guest before deciding the console is stale. */ }
      if (name && name !== this.settings.avd) throw new Error(`Android port is occupied by ${name}. Select that AVD or stop it first.`);
      let boot = '', shellFailed = false;
      try { boot = (await this.adb(['shell', 'getprop', 'sys.boot_completed'], { timeout: 8000 })).trim(); }
      catch { shellFailed = true; }
      if (!boot && !shellFailed) {
        const deadline = Date.now() + 60000;
        while (Date.now() < deadline && boot !== '1') {
          await new Promise(resolve => setTimeout(resolve, 3000));
          try { boot = (await this.adb(['shell', 'getprop', 'sys.boot_completed'], { timeout: 8000 })).trim(); }
          catch { break; }
        }
      }
      if (boot !== '1') {
        if (this.settings.avd !== 'axrb-managed-api36' || !shellFailed) throw new Error('Android is still starting or is unresponsive. Wait for it to finish, then try again.');
        onOutput('Android is unresponsive; restarting the AXRB emulator with a cold boot.\n');
        await this.startEmulator({ onOutput, coldBoot: true, recoverUnresponsive: true });
        return;
      }
      if (!name) throw new Error('Android answered shell commands, but its emulator console is unavailable. Restart Android and try again.');
      const cores = Number((await this.adb(['shell', 'getconf', '_NPROCESSORS_ONLN'], { timeout: 8000 })).trim());
      if (cores !== (this.settings.cpuCores ?? 4)) throw new Error('Restart Android to apply the selected vCPU count.');
      return;
    }
    try { await this.startEmulator({ onOutput }); }
    catch (error) {
      // The guest may still be doing first-boot work at the boot deadline.
      // Leave that live process alone rather than turning a slow boot into a kill.
      if (/did not finish booting within/i.test(error.message)) {
        throw new Error(`${error.message}\nAndroid may still be starting. AXRB left it running; wait or close it manually before retrying.`);
      }
      if (this.settings.avd !== 'axrb-managed-api36' || !/timed out|could not connect to TCP port|actively refused|device offline|device .*not found|is already running; use Verify/i.test(error.message)) throw error;
      onOutput('Android stopped responding during startup; retrying once with a cold boot.\n');
      await this.startEmulator({ onOutput, coldBoot: true, recoverUnresponsive: true });
    }
  }
  async permissions(packageName) {
    validPackage(packageName);
    const [dump, activities] = await Promise.all([
      this.adb(['shell', 'dumpsys', 'package', packageName], { timeout: 20000 }),
      this.adb(['shell', 'dumpsys', 'activity', 'activities'], { timeout: 20000 }).catch(() => ''),
    ]);
    return { items: parseRuntimePermissions(dump), prompt: parsePermissionPrompt(activities) };
  }
  async setPermission(packageName, permission, granted) {
    validPackage(packageName);
    validPermission(permission);
    if (typeof granted !== 'boolean') throw new Error('Invalid permission state.');
    try {
      await this.adb(['shell', 'pm', granted ? 'grant' : 'revoke', '--user', '0', packageName, permission], { timeout: 20000 });
    } catch (error) {
      throw new Error(describePermissionFailure(error.message, permission));
    }
    const result = await this.permissions(packageName);
    if (result.items.find(item => item.name === permission)?.granted !== granted) {
      throw new Error(describePermissionFailure('', permission));
    }
    return result;
  }
  async inspect(apk, { allowSplit = false } = {}) {
    const data = JSON.parse(await run('python', [path.join(this.root, 'launcher/inspect_apk.py'), '--apk', apk, '--sdk', this.settings.sdk, ...(allowSplit ? ['--allow-split'] : [])]));
    validPackage(data.package);
    return { ...data, apk: path.resolve(apk), source: 'local', id: `local:${data.package}` };
  }
  async installed() {
    if (!await this.online()) return null;
    return new Set((await this.adb(['shell', 'pm', 'list', 'packages', '-3'])).split(/\r?\n/).map(s => s.replace(/^package:/, '').trim()).filter(Boolean));
  }
  async importInstalled(packageName, imagePath) {
    validPackage(packageName);
    const activity = (await this.adb(['shell', 'cmd', 'package', 'resolve-activity', '--brief', packageName])).split(/\r?\n/).find(s => s.startsWith(`${packageName}/`));
    if (!activity) return null;
    const metadata = JSON.parse(await run('python', [path.join(this.root, 'scripts/run/android_app_label.py'), '--sdk', this.settings.sdk,
      '--serial', `emulator-${this.settings.port}`, '--package', packageName, '--icon-output', imagePath]));
    return { id: `local:${packageName}`, package: packageName, activity, name: metadata.label, source: 'installed', installed: true,
      image: metadata.icon ? `data:image/png;base64,${(await fs.readFile(metadata.icon)).toString('base64')}` : '' };
  }
  // Quest builds often lack a phone LAUNCHER entry, so a bare query that
  // finds nothing falls back to explicit MAIN/INFO and MAIN/Oculus-VR
  // queries, mirroring the APK inspector's accepted entry points. Transport
  // failures propagate; an unresolvable activity never borrows local metadata.
  async installedActivity(packageName) {
    const prefix = `${packageName}/`;
    const queries = [
      ['shell', 'cmd', 'package', 'resolve-activity', '--brief', packageName],
      ['shell', 'cmd', 'package', 'resolve-activity', '--brief', '-a', 'android.intent.action.MAIN', '-c', 'android.intent.category.INFO', packageName],
      ['shell', 'cmd', 'package', 'resolve-activity', '--brief', '-a', 'android.intent.action.MAIN', '-c', 'com.oculus.intent.category.VR', packageName],
    ];
    for (const args of queries) {
      const output = await this.adb(args, { timeout: PACKAGE_QUERY_TIMEOUT, timeoutMessage: PACKAGE_QUERY_TIMEOUT_MESSAGE });
      const activity = String(output ?? '').split(/\r?\n/).map(line => line.trim()).find(line => line.startsWith(prefix));
      if (activity) return activity;
    }
    throw new Error(`Could not resolve the launch activity for ${packageName}. Refresh your library.`);
  }
  // Resolve Play's launch inputs from the installed build on every call, so an
  // externally updated version can never inherit a stale profile. The returned
  // game is a copy; the caller's record is never rewritten and nothing is
  // cached. ownsEmulator reports whether this call booted Android, so the
  // session script keeps its shutdown ownership after preparation.
  // Android compiles a freshly installed game in the background, and both
  // queries below take the package-manager lock that the compiler holds.
  // Measured at 159 seconds for one title, so a budget in tens of seconds
  // turns "still working" into "the runtime is broken".
  async optimising() {
    try { return (await this.adb(['shell', 'getprop', 'init.svc.artd'], { timeout: 5000 })).trim() === 'running'; }
    catch { return false; }
  }
  async prepareLaunch(game) {
    if (this.child) throw new Error('A game is already running.');
    validPackage(game?.package);
    const wasOnline = await this.online();
    await this.ensure();
    const ownsEmulator = !wasOnline;
    try {
      const identity = parseInstalledIdentity(await this.adb(['shell', 'dumpsys', 'package', game.package], { timeout: PACKAGE_QUERY_TIMEOUT, requireCompleteOutput: true, timeoutMessage: PACKAGE_QUERY_TIMEOUT_MESSAGE }), game.package);
      const activity = await this.installedActivity(game.package);
      return { game: { ...game, package: identity.package, version: identity.version, versionCode: identity.versionCode, activity }, ownsEmulator };
    } catch (error) {
      // Ask before tearing anything down: the answer is lost once the
      // emulator is gone, and "still optimising" is the difference between
      // waiting a minute and hunting a fault that is not there.
      const busy = error.message === PACKAGE_QUERY_TIMEOUT_MESSAGE && await this.optimising();
      // A newly booted emulator is torn down; a pre-existing runtime is never
      // touched. Startup failures from ensure() above keep its own contract.
      if (ownsEmulator) await this.adb(['emu', 'kill']).catch(() => {});
      throw busy ? new Error(`Android is still optimising ${game.package} after installation. That runs once per install and can take several minutes; start the game again when it finishes.`) : error;
    }
  }
  async install(game, update = () => {}) {
    validPackage(game.package);
    if (this.child) throw new Error('Close the running game before installing.');
    if (!game.apk) throw new Error('Import or download an APK first.');
    update('Starting Android'); await this.ensure();
    await installFiles(game, (args, options) => this.adb(args, options), update);
  }
  async uninstall(game, update = () => {}) {
    validPackage(game.package);
    if (game.package.startsWith('com.axrb.')) throw new Error('The AXRB runtime cannot be uninstalled here.');
    if (this.child) throw new Error('Close the running game before uninstalling.');
    update('Starting Android'); await this.ensure();
    const installed = await this.installed();
    if (!installed) throw new Error('Android disconnected. Try again.');
    if (!installed.has(game.package)) throw new Error('Game is not installed. Refresh your library.');
    update('Uninstalling');
    const result = await this.adb(['uninstall', game.package], { timeout: 240000 });
    if (!/^Success\s*$/m.test(result)) throw new Error(result.trim() || 'Android did not confirm the uninstall.');
  }
  launch(game, onExit, compatibility = {}, options = {}) {
    if (this.child) throw new Error('A game is already running.');
    validPackage(game.package);
    const bridge = options.bridge;
    if (!bridge && (!/^[A-Za-z0-9_./]+$/.test(game.activity || '') || game.activity.split('/')[0] !== game.package)) throw new Error('Invalid launch activity. Refresh installed games.');
    // Preparation may have booted Android before the session script runs, so
    // ownership arrives explicitly instead of being inferred from the device.
    const ownsEmulator = options?.ownsEmulator === true;
    const sessionId = options?.sessionId;
    if (sessionId !== undefined && !/^[a-f0-9]{8}$/.test(sessionId)) throw new Error('Invalid session identifier.');
    this.fpsHudEvent = `Local\\AXRB.FpsHud.${randomUUID().replaceAll('-', '')}`;
    const args = bridge ? [] : powershellArgs(path.join(this.root, 'scripts/run/run_windows_game.ps1'), { Avd: this.settings.avd, Port: this.settings.port,
      Sdk: this.settings.sdk, MemoryMB: this.settings.memoryMB, CpuCores: this.settings.cpuCores ?? 4, Package: game.package, Activity: game.activity, GameName: game.name, FpsHud: this.settings.fpsHud === true, FpsHudEventName: this.fpsHudEvent,
      // Recovery inside the game script must use the same CPU policy as ensure().
      GuestClock: this.settings.guestClock || 'Auto',
      ...(this.settings.localApic ? { LocalApic: this.settings.localApic } : {}),
      // An absent switch keeps the script's historic device-presence behavior.
      ...(ownsEmulator ? { OwnsEmulator: true } : {}),
      ...(sessionId ? { SessionId: sessionId } : {}),
      ...(this.settings.precomposeProjectionLayers === true || compatibility.precomposeProjectionLayers === true ? { PrecomposeProjectionLayers: true } : {}),
      ...(this.settings.managedDirectory ? { RuntimeApk: path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk') } : {}) });
    // Windows PowerShell can exit successfully without executing its command
    // when CREATE_NEW_PROCESS_GROUP/detached is combined with no console.
    this.bridgeStopEvent = bridge ? `Local\\AXRB.QuestBridge.${randomUUID().replaceAll('-', '')}` : null;
    if (bridge) this.fpsHudEvent = null;
    const child = bridge
      ? spawn(bridge.executable, [path.join(bridge.root, bridge.library), bridge.entry], {
        windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], cwd: path.dirname(bridge.executable),
        env: bridgeEnvironment(process.env, bridge, this.bridgeStopEvent) })
      : spawn('powershell.exe', args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    this.child = child; this.game = game.id;
    let tail = '';
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    const stdout = { stream: `${child.pid}:stdout`, tag: 'game launch' }, stderr = { stream: `${child.pid}:stderr`, tag: 'game launch', level: 'E' };
    child.stdout.on('data', text => { tail = (tail + text).slice(-4000); this.onOutput(text, stdout); });
    child.stderr.on('data', text => { tail = (tail + text).slice(-4000); this.onOutput(text, stderr); });
    let finished = false, spawnFailed = false, drain = null, stdoutEnded = false, stderrEnded = false;
    const endStdout = () => { if (stdoutEnded) return; stdoutEnded = true; this.onOutput('', { ...stdout, end: true }); };
    const endStderr = () => { if (stderrEnded) return; stderrEnded = true; this.onOutput('', { ...stderr, end: true }); };
    child.stdout.once('close', endStdout);
    child.stderr.once('close', endStderr);
    const end = (code, error) => {
      if (finished) return;
      finished = true; clearTimeout(drain);
      child.stdout.destroy(); child.stderr.destroy();
      endStdout(); endStderr();
      this.child = null; this.game = null; this.fpsHudEvent = null; this.bridgeStopEvent = null;
      onExit(code, error || tail);
    };
    child.on('error', async e => {
      spawnFailed = true;
      // Keep the session occupied until cleanup finishes; otherwise a new Play
      // could start an emulator that this failed launch would then shut down.
      if (ownsEmulator) await this.adb(['emu', 'kill']).catch(error => this.onOutput(`Android shutdown failed: ${error.message}\n`));
      end(1, e.message);
    });
    // Like run(), drain briefly after exit rather than waiting indefinitely
    // for an emulator or host descendant to release inherited pipe handles.
    child.on('exit', code => { if (!finished && !spawnFailed) drain = setTimeout(() => end(code), 500); });
    child.on('close', code => { if (!spawnFailed) end(code); });
  }
  async setFpsHud(enabled) {
    if (typeof enabled !== 'boolean') throw new Error('Invalid FPS HUD setting.');
    if (this.child && this.fpsHudEvent) {
      await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/run/fps_hud.ps1'), {
        EventName: this.fpsHudEvent, Enabled: enabled ? 1 : 0
      }), { timeout: 5000 });
    }
    this.settings.fpsHud = enabled;
  }
  async stopEmulator() {
    const child = this.child;
    if (child) {
      await this.stop();
      const deadline = Date.now() + 60000;
      while (this.child === child && child.exitCode === null && child.signalCode === null) {
        if (Date.now() >= deadline) throw new Error('The game is still stopping. Wait for it to finish, then close AXRB again.');
        await new Promise(resolve => setTimeout(resolve, 250));
      }
    }
    if (!(await this.status()).running) return;
    await this.adb(['emu', 'kill'], { timeout: 10000 });
    const deadline = Date.now() + 180000;
    while ((await this.status()).running) {
      if (Date.now() >= deadline) throw new Error('The emulator has not finished shutting down. AXRB will stay open; try again after it finishes saving.');
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
  async stop() {
    const pid = this.child?.pid;
    if (!pid) return;
    if (this.bridgeStopEvent) {
      await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/run/stop_quest_bridge.ps1'), { EventName: this.bridgeStopEvent, ProcessId: pid }), { timeout: 40000, onOutput: this.onOutput });
      return;
    }
    await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/run/stop_game.ps1'), { ParentPid: pid }));
  }
}
