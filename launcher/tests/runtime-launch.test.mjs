import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Runtime, run } from '../core/runtime.mjs';
import { loadCompatibilityProfiles, resolveCompatibility, compatibilityRuntimeOptions } from '../core/compatibility.mjs';

test('status() reports a stopped, unreachable emulator without throwing', { skip: process.platform !== 'win32', timeout: 15000 }, async () => {
  const root = fileURLToPath(new URL('../..', import.meta.url));
  const runtime = new Runtime(root, { avd: 'axrb-managed-api36', port: 5584, sdk: 'C:\\fixture\\does-not-exist', memoryMB: 8192 });
  assert.deepEqual(await runtime.status(), { running: false, pid: null, count: 0, adbState: '' });
});

test('install readiness recovers only an unresponsive managed emulator', async () => {
  class ProbeRuntime extends Runtime {
    constructor(avd, responses) {
      super('fixture', { avd, port: 5584, sdk: 'fixture', memoryMB: 8192 });
      this.responses = responses;
      this.starts = [];
    }
    async adb(args) {
      const key = args.join(' ');
      const response = this.responses[key];
      if (response instanceof Error) throw response;
      return response;
    }
    async startEmulator(options) { this.starts.push(options); }
  }
  const stale = new ProbeRuntime('axrb-managed-api36', {
    'get-state': 'device', 'emu avd name': new Error('port refused'),
    'shell getprop sys.boot_completed': new Error('timed out'),
  });
  await stale.ensure();
  assert.equal(stale.starts.length, 1);
  assert.equal(stale.starts[0].coldBoot, true);
  assert.equal(stale.starts[0].recoverUnresponsive, true);

  const healthy = new ProbeRuntime('axrb-managed-api36', {
    'get-state': 'device', 'emu avd name': 'axrb-managed-api36\nOK',
    'shell getprop sys.boot_completed': '1', 'shell getconf _NPROCESSORS_ONLN': '4',
  });
  await healthy.ensure();
  assert.equal(healthy.starts.length, 0);

  const other = new ProbeRuntime('other-avd', stale.responses);
  await assert.rejects(other.ensure(), /still starting or is unresponsive/);
  assert.equal(other.starts.length, 0);
});

test('failed managed startup gets one cold boot retry', async () => {
  class ProbeRuntime extends Runtime {
    constructor() {
      super('fixture', { avd: 'axrb-managed-api36', port: 5584, sdk: 'fixture', memoryMB: 8192 });
      this.starts = [];
    }
    async online() { return false; }
    async startEmulator(options) {
      this.starts.push(options);
      if (this.starts.length === 1) throw new Error('adb.exe timed out after 90 seconds');
    }
  }
  const runtime = new ProbeRuntime();
  await runtime.ensure();
  assert.equal(runtime.starts.length, 2);
  assert.equal(runtime.starts[0].coldBoot, undefined);
  assert.equal(runtime.starts[1].coldBoot, true);
  assert.equal(runtime.starts[1].recoverUnresponsive, true);
});

test('a slow but still running Android boot is never stopped for an automatic retry', async () => {
  class ProbeRuntime extends Runtime {
    constructor() {
      super('fixture', { avd: 'axrb-managed-api36', port: 5584, sdk: 'fixture', memoryMB: 8192 });
      this.starts = 0;
    }
    async online() { return false; }
    async startEmulator() {
      this.starts++;
      throw new Error('Android did not finish booting within 8 minutes.');
    }
  }
  const runtime = new ProbeRuntime();
  await assert.rejects(runtime.ensure(), /AXRB left it running/);
  assert.equal(runtime.starts, 1);
});


test('Windows game launch actually executes PowerShell and reports its exit', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-launch-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'scripts/run'), { recursive: true });
  await fs.writeFile(path.join(root, 'scripts/run/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName)
Write-Output "EXECUTED:$Package CORES:$CpuCores"
exit 7
`);
  const runtime = new Runtime(root, { avd:'test',port:5580,sdk:root,memoryMB:8192,cpuCores:6 });
  const result = await new Promise(resolve => {
    runtime.launch({ id:'local:com.example.game',package:'com.example.game',activity:'com.example.game/.Main',name:'Test' }, (code, output) => resolve({code,output}));
    t.after(() => runtime.child?.kill());
  });
  assert.equal(result.code, 7); // -File propagates the script's own exit code.
  assert.match(result.output, /EXECUTED:com.example.game/);
  assert.match(result.output, /CORES:6/);
  assert.equal(runtime.game, null);
});

test('projection precomposition follows matched profiles or the global override', { skip: process.platform !== 'win32', timeout: 20000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-profile-launch-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'scripts/run'), { recursive: true });
  await fs.writeFile(path.join(root, 'scripts/run/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName, [switch]$PrecomposeProjectionLayers)
Write-Output "PRECOMPOSE:$PrecomposeProjectionLayers"
`);
  const profiles = await loadCompatibilityProfiles(new URL('../core/game-compatibility.json', import.meta.url));
  const runtime = new Runtime(root, { avd: 'test', port: 5580, sdk: root, memoryMB: 8192, precomposeProjectionLayers: false });
  const climb = { id: 'climb', package: 'com.crytek.climb2', activity: 'com.crytek.climb2/.Main', name: 'The Climb 2', version: '2.2' };
  const unrelated = { id: 'other', package: 'com.example.game', activity: 'com.example.game/.Main', name: 'Other' };
  async function launch(game) {
    const result = await new Promise(resolve => {
      runtime.launch(game, (code, output) => resolve({ code, output }), compatibilityRuntimeOptions(resolveCompatibility(profiles, game)));
      t.after(() => runtime.child?.kill());
    });
    assert.equal(result.code, 0, result.output);
    return result.output;
  }
  assert.match(await launch(climb), /PRECOMPOSE:True/);
  assert.match(await launch(unrelated), /PRECOMPOSE:False/);
  assert.match(await launch({ ...climb, version: '2.3' }), /PRECOMPOSE:False/);
  runtime.settings.precomposeProjectionLayers = true;
  assert.match(await launch(unrelated), /PRECOMPOSE:True/);
});

test('FPS HUD can be switched live through the session event', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-hud-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'scripts/run'), { recursive: true });
  await fs.copyFile(new URL('../../scripts/run/fps_hud.ps1', import.meta.url), path.join(root, 'scripts/run/fps_hud.ps1'));
  await fs.writeFile(path.join(root, 'scripts/run/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName)
$eventHandle = [System.Threading.EventWaitHandle]::new([bool]$FpsHud, [System.Threading.EventResetMode]::ManualReset, $FpsHudEventName)
try {
  if ($eventHandle.WaitOne(0)) { throw 'Expected HUD off initially' }
  if (!$eventHandle.WaitOne(5000)) { throw 'HUD never enabled' }
  Write-Output 'HUD_ON'
  $deadline = [DateTime]::UtcNow.AddSeconds(5)
  while ($eventHandle.WaitOne(0)) { if ([DateTime]::UtcNow -gt $deadline) { throw 'HUD never disabled' }; Start-Sleep -Milliseconds 10 }
  Write-Output 'HUD_OFF'
} finally { $eventHandle.Dispose() }
`);
  const runtime = new Runtime(root, { avd: 'test', port: 5580, sdk: root, memoryMB: 8192, fpsHud: false });
  let enabled;
  const on = new Promise(resolve => { enabled = resolve; });
  const ended = new Promise(resolve => {
    runtime.launch({ id: 'test', package: 'com.example.game', activity: 'com.example.game/.Main', name: 'Test' }, (code, output) => resolve({ code, output }));
    runtime.child.stdout.on('data', data => { if (data.toString().includes('HUD_ON')) enabled(); });
    t.after(() => runtime.child?.kill());
  });
  await runtime.setFpsHud(true); await on;
  await runtime.setFpsHud(false);
  const result = await ended;
  assert.equal(result.code, 0, result.output);
  assert.match(result.output, /HUD_OFF/);
  assert.equal(runtime.settings.fpsHud, false);
  await assert.rejects(runtime.setFpsHud('true'), /Invalid/);
});

test('run resolves when a long-lived grandchild keeps the stdio pipes open', { skip: process.platform !== 'win32', timeout: 20000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-pipes-'));
  let grandchild = 0;
  // The grandchild holds its redirected log files until it is gone, so stop it
  // before removing the directory and let Windows release the handles.
  t.after(async () => {
    try { process.kill(grandchild); } catch {}
    await fs.rm(root, { recursive: true, force: true, maxRetries: 30, retryDelay: 100 });
  });
  // The Android emulator inherits the launching script's pipes and outlives it,
  // so 'close' never fires. Settling on that event stalled setup until timeout.
  const script = path.join(root, 'spawner.ps1');
  await fs.writeFile(script, [
    `$out = '${path.join(root, 'child.out')}'`,
    `$err = '${path.join(root, 'child.err')}'`,
    "$p = Start-Process powershell.exe -ArgumentList '-NoProfile','-Command','Start-Sleep -Seconds 60' -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err",
    'Write-Output "Ready: $($p.Id)"',
  ].join('\n'));
  const started = Date.now();
  const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script], { timeout: 15000 });
  assert.match(output, /^Ready: \d+/m, 'the script transcript must survive the early settle');
  assert.ok(Date.now() - started < 10000, 'run must not wait for the grandchild to exit');
  grandchild = Number(output.match(/^Ready: (\d+)/m)[1]);
});

test('run still reports a failing exit code and its stderr', { skip: process.platform !== 'win32', timeout: 15000 }, async () => {
  await assert.rejects(
    run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', '[Console]::Error.WriteLine("boom"); exit 3']),
    /boom/);
});

function installedDumpsys({ code = '100', name = '2.2', user0 = 'User 0: ceDataInode=1 installed=true' } = {}) {
  const lines = ['Packages:', '  Package [com.example.game] (abc123):', '    userId=12345'];
  if (code !== null) lines.push(`    versionCode=${code} targetSdk=34`);
  if (name !== null) lines.push(`    versionName=${name}`);
  if (user0 !== null) lines.push(`    ${user0}`);
  lines.push('');
  return lines.join('\n');
}

class PrepareProbe extends Runtime {
  constructor({ dumpsys = '', activities = {}, online = true, dumpsysError = null } = {}) {
    super('fixture', { avd: 'test-avd', port: 5584, sdk: 'fixture', memoryMB: 8192 });
    this.dumpsys = dumpsys;
    this.activities = activities;
    this._online = online;
    this.ensureCalls = 0;
    this.adbCalls = [];
    this.dumpsysError = dumpsysError;
  }
  async online() { return this._online; }
  async ensure() { this.ensureCalls++; }
  async adb(args) {
    const key = args.join(' ');
    this.adbCalls.push(key);
    if (key === 'shell dumpsys package com.example.game') {
      if (this.dumpsysError) throw this.dumpsysError;
      return this.dumpsys;
    }
    if (key.startsWith('shell cmd package resolve-activity')) {
      const out = this.activities[key];
      if (out instanceof Error) throw out;
      if (out !== undefined) return out;
      return 'No activity found';
    }
    if (key === 'emu kill') return '';
    throw new Error(`unexpected adb call: ${key}`);
  }
}

const BARE_QUERY = 'shell cmd package resolve-activity --brief com.example.game';
const INFO_QUERY = 'shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.INFO com.example.game';
const VR_QUERY = 'shell cmd package resolve-activity --brief -a android.intent.action.MAIN -c com.oculus.intent.category.VR com.example.game';

test('prepareLaunch resolves live installed identity without touching local metadata', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: '1.0-local', versionCode: '1', activity: 'com.example.game/.Local', name: 'Test' };
  const before = { ...local };
  const runtime = new PrepareProbe({
    dumpsys: installedDumpsys({ code: '9007199254740993', name: '2.2-live' }),
    activities: { [BARE_QUERY]: 'priority=0\ncom.example.game/.Live\n' },
  });
  const prepared = await runtime.prepareLaunch(local);
  assert.equal(runtime.ensureCalls, 1);
  assert.equal(prepared.ownsEmulator, false);
  assert.equal(prepared.game.package, 'com.example.game');
  assert.equal(prepared.game.version, '2.2-live');
  assert.equal(prepared.game.versionCode, '9007199254740993');
  assert.equal(typeof prepared.game.versionCode, 'string');
  assert.equal(prepared.game.activity, 'com.example.game/.Live');
  assert.equal(prepared.game.id, local.id);
  assert.deepEqual(local, before);
});

test('prepareLaunch re-reads the installed build on every call', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: 'stale', versionCode: '0', activity: 'com.example.game/.Local', name: 'Test' };
  const runtime = new PrepareProbe({
    dumpsys: installedDumpsys({ code: '100', name: '2.2' }),
    activities: { [BARE_QUERY]: 'com.example.game/.Live\n' },
  });
  const first = await runtime.prepareLaunch(local);
  assert.equal(first.game.version, '2.2');
  assert.equal(first.game.versionCode, '100');
  runtime.dumpsys = installedDumpsys({ code: '101', name: '2.3' });
  const second = await runtime.prepareLaunch(local);
  assert.equal(second.game.version, '2.3');
  assert.equal(second.game.versionCode, '101');
  assert.equal(first.game.version, '2.2');
  assert.equal(local.version, 'stale');
});

test('prepareLaunch scopes identity to the current package record and owner install', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: 'stale', versionCode: '0', activity: 'com.example.game/.Local', name: 'Test' };
  const live = { [BARE_QUERY]: 'com.example.game/.Live\n' };
  const current = installedDumpsys({ code: '200', name: '3.0' });
  const hiddenFirst = ['Hidden system packages:', '  Package [com.example.game] (old):', '    versionCode=1', '    versionName=stale-hidden', '    User 0: installed=true', '', current].join('\n');
  const hiddenLast = [current, 'Hidden system packages:', '  Package [com.example.game] (old):', '    versionCode=1', '    versionName=stale-hidden', '    User 0: installed=true', ''].join('\n');
  for (const dumpsys of [hiddenFirst, hiddenLast]) {
    const runtime = new PrepareProbe({ dumpsys, activities: live });
    const prepared = await runtime.prepareLaunch(local);
    assert.equal(prepared.game.version, '3.0');
    assert.equal(prepared.game.versionCode, '200');
  }
  const sibling = installedDumpsys().replaceAll('com.example.game', 'com.example.game2');
  await assert.rejects(new PrepareProbe({ dumpsys: sibling, activities: live }).prepareLaunch(local), /not installed/);
  await assert.rejects(new PrepareProbe({ dumpsys: 'Packages:\n', activities: live }).prepareLaunch(local), /not installed/);
  const duplicate = [installedDumpsys({ code: '200', name: '3.0' }), '  Package [com.example.game] (other):', '    versionCode=200', '    versionName=3.0', '    User 0: installed=true', ''].join('\n');
  await assert.rejects(new PrepareProbe({ dumpsys: duplicate, activities: live }).prepareLaunch(local), /Multiple installed records/);
  const notOwner = installedDumpsys({ user0: 'User 0: ceDataInode=1 installed=false' }) + '    User 10: ceDataInode=2 installed=true\n';
  await assert.rejects(new PrepareProbe({ dumpsys: notOwner, activities: live }).prepareLaunch(local), /not installed/);
  await assert.rejects(new PrepareProbe({ dumpsys: installedDumpsys({ code: null }), activities: live }).prepareLaunch(local), /installed version/);
  const noName = await new PrepareProbe({ dumpsys: installedDumpsys({ name: null }), activities: live }).prepareLaunch(local);
  assert.equal(noName.game.version, '');
  assert.equal(noName.game.versionCode, '100');
});

test('prepareLaunch never falls back to local metadata on read failure', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: '1.0-local', versionCode: '1', activity: 'com.example.game/.Local', name: 'Test' };
  const live = { [BARE_QUERY]: 'com.example.game/.Live\n' };
  await assert.rejects(new PrepareProbe({ dumpsysError: new Error('adb shell failed'), activities: live }).prepareLaunch(local), /adb shell failed/);
  assert.equal(local.version, '1.0-local');
  const runtime = new PrepareProbe({ dumpsys: installedDumpsys(), activities: {} });
  await assert.rejects(runtime.prepareLaunch(local), /launch activity/);
  assert.equal(local.activity, 'com.example.game/.Local');
  assert.ok(!runtime.adbCalls.some(call => call.includes('.Local')));
});

test('prepareLaunch resolves Quest activities without a phone launcher entry', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: 'stale', versionCode: '0', activity: 'com.example.game/.Local', name: 'Test' };
  const dumpsys = installedDumpsys();
  const bare = await new PrepareProbe({ dumpsys, activities: { [BARE_QUERY]: 'com.example.game/.Bare\n' } }).prepareLaunch(local);
  assert.equal(bare.game.activity, 'com.example.game/.Bare');
  const info = await new PrepareProbe({ dumpsys, activities: { [BARE_QUERY]: 'No activity found\n', [INFO_QUERY]: 'com.example.game/.Info\n' } }).prepareLaunch(local);
  assert.equal(info.game.activity, 'com.example.game/.Info');
  const vr = await new PrepareProbe({ dumpsys, activities: { [BARE_QUERY]: 'No activity found\n', [INFO_QUERY]: 'No activity found\n', [VR_QUERY]: 'com.example.game/.Vr\n' } }).prepareLaunch(local);
  assert.equal(vr.game.activity, 'com.example.game/.Vr');
  const probe = new PrepareProbe({ dumpsys, activities: { [BARE_QUERY]: 'com.other/.Other\n', [INFO_QUERY]: 'No activity found\n', [VR_QUERY]: 'No activity found\n' } });
  await assert.rejects(probe.prepareLaunch(local), /launch activity/);
  assert.equal(local.activity, 'com.example.game/.Local');
});

test('prepareLaunch owns only emulators it boots', async () => {
  const local = { id: 'local:com.example.game', package: 'com.example.game', version: 'stale', versionCode: '0', activity: 'com.example.game/.Local', name: 'Test' };
  const live = { [BARE_QUERY]: 'com.example.game/.Live\n' };
  const coldFailure = new PrepareProbe({ online: false, dumpsysError: new Error('adb shell failed'), activities: live });
  await assert.rejects(coldFailure.prepareLaunch(local), /adb shell failed/);
  assert.ok(coldFailure.adbCalls.includes('emu kill'));
  const warmFailure = new PrepareProbe({ online: true, dumpsysError: new Error('adb shell failed'), activities: live });
  await assert.rejects(warmFailure.prepareLaunch(local), /adb shell failed/);
  assert.ok(!warmFailure.adbCalls.includes('emu kill'));
  const cold = new PrepareProbe({ online: false, dumpsys: installedDumpsys(), activities: live });
  const preparedCold = await cold.prepareLaunch(local);
  assert.equal(preparedCold.ownsEmulator, true);
  assert.ok(!cold.adbCalls.includes('emu kill'));
  const warm = new PrepareProbe({ online: true, dumpsys: installedDumpsys(), activities: live });
  const preparedWarm = await warm.prepareLaunch(local);
  assert.equal(preparedWarm.ownsEmulator, false);
  assert.ok(!warm.adbCalls.includes('emu kill'));
  const busy = new PrepareProbe({ dumpsys: installedDumpsys(), activities: live });
  busy.child = {};
  await assert.rejects(busy.prepareLaunch(local), /already running/);
  assert.equal(busy.ensureCalls, 0);
});

test('failed launch retains session ownership until emulator cleanup completes', { skip: process.platform !== 'win32', timeout: 10000 }, async () => {
  const runtime = new Runtime('missing-fixture', { avd: 'test', port: 5580, sdk: 'missing-fixture', memoryMB: 8192 });
  const game = { id: 'test', package: 'com.example.game', activity: 'com.example.game/.Main' };
  let finishCleanup, cleanupStarted;
  const cleanup = new Promise(resolve => { finishCleanup = resolve; });
  const started = new Promise(resolve => { cleanupStarted = resolve; });
  runtime.adb = async args => { assert.deepEqual(args, ['emu', 'kill']); cleanupStarted(); await cleanup; };
  let exit;
  const exited = new Promise(resolve => { exit = resolve; });
  const savedPath = process.env.PATH;
  try {
    process.env.PATH = '';
    runtime.launch(game, code => exit(code), {}, { ownsEmulator: true });
  } finally { process.env.PATH = savedPath; }
  const closed = new Promise(resolve => runtime.child.once('close', resolve));
  try {
    await started;
    await closed;
    assert.throws(() => runtime.launch(game, () => {}), /already running/);
  } finally { finishCleanup(); }
  assert.equal(await exited, 1);
  assert.equal(runtime.child, null);
  assert.equal(runtime.game, null);
});

test('a package query blocked by post-install optimisation is reported as optimisation, not a broken runtime', async () => {
  const calls = [];
  const runtime = new Runtime('C:\root', { sdk: 'C:\sdk', port: 5584, avd: 'axrb-managed-api36', cpuCores: 4 });
  runtime.online = async () => true;
  runtime.ensure = async () => {};
  runtime.adb = async (args, options = {}) => {
    calls.push(args.join(' '));
    if (args.includes('dumpsys')) throw new Error(options.timeoutMessage ?? 'timeout');
    if (args.includes('getprop') && args.includes('init.svc.artd')) return 'running\n';
    return '';
  };
  await assert.rejects(
    () => runtime.prepareLaunch({ package: 'com.example.game', installed: true }),
    /still optimising com\.example\.game after installation/);
  assert.ok(calls.some(call => call.includes('init.svc.artd')), 'the optimisation state is checked before giving up');
});

test('a package query that fails for another reason keeps its own error', async () => {
  const runtime = new Runtime('C:\root', { sdk: 'C:\sdk', port: 5584, avd: 'axrb-managed-api36', cpuCores: 4 });
  runtime.online = async () => true;
  runtime.ensure = async () => {};
  runtime.adb = async args => {
    if (args.includes('dumpsys')) throw new Error('device offline');
    if (args.includes('getprop')) return 'stopped\n';
    return '';
  };
  await assert.rejects(() => runtime.prepareLaunch({ package: 'com.example.game', installed: true }), /device offline/);
});

test('an idle device does not blame optimisation for a timeout', async () => {
  const runtime = new Runtime('C:\root', { sdk: 'C:\sdk', port: 5584, avd: 'axrb-managed-api36', cpuCores: 4 });
  runtime.online = async () => true;
  runtime.ensure = async () => {};
  runtime.adb = async (args, options = {}) => {
    if (args.includes('dumpsys')) throw new Error(options.timeoutMessage ?? 'timeout');
    if (args.includes('getprop')) return 'stopped\n';
    return '';
  };
  await assert.rejects(() => runtime.prepareLaunch({ package: 'com.example.game', installed: true }), /did not answer a package query in time/);
});

test('quitting stops the ADB server only when no game or emulator still needs it', async t => {
  const sdk = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-adb-'));
  t.after(() => fs.rm(sdk, { recursive: true, force: true }));
  const runtime = new Runtime('', { avd: 'axrb-managed-api36', port: 5584, sdk });
  let running = false;
  runtime.status = async () => ({ running, pid: null, count: running ? 1 : 0, adbState: '' });
  assert.equal(await runtime.adbServerIdle(), false, 'no adb is installed yet');
  await fs.mkdir(path.join(sdk, 'platform-tools'));
  await fs.writeFile(path.join(sdk, 'platform-tools', 'adb.exe'), '');
  assert.equal(await runtime.adbServerIdle(), true);
  running = true;
  assert.equal(await runtime.adbServerIdle(), false, 'the emulator still runs');
  running = false; runtime.child = {};
  assert.equal(await runtime.adbServerIdle(), false, 'a game session still streams through adb reverse');
  runtime.child = null; running = true;
  runtime.status = async () => { throw new Error('the watchdog reading should have been used'); };
  assert.equal(await runtime.adbServerIdle('stopped'), true, "the watchdog's reading spares a status run");
  assert.equal(await runtime.adbServerIdle('online'), false);
  assert.equal(await runtime.adbServerIdle('starting'), false);
  runtime.status = async () => ({ running: false, pid: null, count: 0, adbState: '' });
  assert.equal(await runtime.adbServerIdle('unknown'), true, 'an unknown reading asks directly');
});
