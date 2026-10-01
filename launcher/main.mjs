import { app, BrowserWindow, ipcMain, dialog, shell, safeStorage, session, clipboard } from 'electron';
import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID, createHash } from 'node:crypto';
import { MetaAuth, QuestStore, appId } from './core/meta.mjs';
import { downloadFile, safeName, checkSpace } from './core/download.mjs';
import { State } from './core/state.mjs';
import { Runtime, run } from './core/runtime.mjs';
import { prepareBridge } from './core/quest-bridge.mjs';
import { Setup, identifyArchives, avdDirectory, parseStorageGB, planStorageChange, withStorageGB } from './core/setup.mjs';
import { loadLibraryArtwork } from './core/artwork.mjs';
import { Quest } from './core/quest.mjs';
import { findOriginalApk, importGameZip } from './core/game-files.mjs';
import { collectDiagnostics, describeSessionEnd, parseSessionRecord, redact, uploadDiagnostics } from './core/diagnostics.mjs';
import { LiveDiagnostics } from './core/live-diagnostics.mjs';
import { EmulatorWatchdog } from './core/watchdog.mjs';
import { createCloseRequest } from './core/close-request.mjs';
import { migrateClockPolicy } from './core/clock-policy.mjs';
import { loadCompatibilityProfiles, resolveCompatibility, compatibilityPatchArgs, compatibilityRuntimeOptions } from './core/compatibility.mjs';
import { Ovrport, selectedPatchArgs } from './core/ovrport.mjs';
import { MetaSession } from './core/session.mjs';
import { carryPortableFiles, configurePortable, portableOutput, sweepPortableTemp } from './core/portable.mjs';
import { performanceScanArgs, performanceScanTimeout } from './core/performance.mjs';
import { ChatGPTAuth } from './core/chatgpt-auth.mjs';
import { AiDiagnostics, cleanEvidence, checkedText, MAX_EVIDENCE } from './core/ai-diagnostics.mjs';
import { DiagnosticHarness } from './core/ai-tools.mjs';
import electronUpdater from 'electron-updater';
import { LauncherUpdates } from './core/updates.mjs';

// Keep the packaged app in Electron GUI mode even when launched from a shell
// that uses ELECTRON_RUN_AS_NODE for other tooling.
delete process.env.ELECTRON_RUN_AS_NODE;
const directory = path.dirname(fileURLToPath(import.meta.url)), root = app.isPackaged ? path.join(process.resourcesPath, 'runtime') : path.dirname(directory);
if (app.isPackaged) process.env.PATH = path.join(root, 'tools/python') + path.delimiter + process.env.PATH;
// Helper scripts report paths and APK labels as JSON on stdout. A host locale
// such as cp1252 cannot encode every path AXRB may be extracted to, so pin all
// Python children to UTF-8 instead of inheriting the machine's code page.
process.env.PYTHONUTF8 = '1';
const smoke = process.argv.includes('--smoke-test');
const debug = process.argv.includes('--axrb-debug') || process.argv.includes('--debug') || process.env.AXRB_DEBUG === '1';
const profile = app.commandLine.getSwitchValue('user-data-dir');
const portable = app.isPackaged && existsSync(path.join(path.dirname(process.execPath), 'AXRB.portable')) ? path.dirname(process.execPath) : '';
if (portable) configurePortable(app, portable);
else if (profile) app.setPath('userData', path.resolve(profile));
else if (smoke) app.setPath('userData', path.join(root, 'out/launcher/smoke'));
else app.setPath('userData', path.join(app.getPath('appData'), 'AXRB'));
if (!app.requestSingleInstanceLock()) app.quit();
app.on('second-instance', () => { window?.show(); window?.focus(); });
let window, authWindow, state, runtime, token = '', account = '', busy = false;
let setup;
let launcherUpdates, installUpdateOnQuit = false;
let metaSession;
let liveDiagnostics, reviewedDiagnostics = null;
let emulatorWatchdog;
let chatgpt, aiDiagnostics;
// Structured per-game-session records (newest first) shipped in every
// diagnostics bundle; the run script also writes logs/game/session.json.
const sessionRecords = [];
const sessionRecordPath = () => path.join(process.env.AXRB_DATA_HOME || path.join(root, 'out'), 'logs/game/session.json');
let quitting = false;
let compatibilityProfiles;
const ovrport = new Ovrport();
// Quitting must not leave a half-copied portable import behind; aborting lets
// carryPortableFiles remove its partial directory before the process goes away.
const shutdown = new AbortController();
app.on('before-quit', event => {
  if (quitting) return;
  event.preventDefault();
  void requestClose();
});
const requestClose = createCloseRequest({
  getStatus: () => runtime ? runtime.status() : Promise.resolve({ running: false }),
  prompt: async () => {
    if (installUpdateOnQuit) {
      const {response}=await dialog.showMessageBox(window,{type:'question',title:'Install AXRB update?',message:'Stop the emulator and install the update?',detail:'The running game will close. Save your progress first.',buttons:['Stop emulator and install','Later'],defaultId:1,cancelId:1,noLink:true});
      return response===0?'stop':'cancel';
    }
    const { response } = await dialog.showMessageBox(window, {
      type: 'question', title: 'Close AXRB?',
      message: 'The Android emulator is still running.',
      detail: 'Stop the emulator before closing? Any running game will also be stopped.',
      buttons: ['Stop emulator and close', 'Leave running and close', 'Cancel'],
      defaultId: 0, cancelId: 2, noLink: true,
    });
    return ['stop', 'leave', 'cancel'][response];
  },
  stop: () => exclusive(async () => {
    if (setup?.status?.active) throw new Error('Wait for runtime setup to finish before stopping the emulator.');
    await runtime.stopEmulator();
  }),
  onError: async error => {
    liveDiagnostics?.append('launcher', `Close cancelled: ${message(error)}`, { tag: 'runtime', level: 'E' });
    await dialog.showMessageBox(window, { type: 'error', title: 'AXRB is still open',
      message: 'Could not finish closing the launcher.', detail: message(error) });
  },
  quit: async () => {
    if (installUpdateOnQuit && (busy || setup?.status?.active || aiDiagnostics?.controller)) throw new Error('Finish the current operation before installing the update.');
    quitting = true;
    shutdown.abort();
    chatgpt?.cancel(); aiDiagnostics?.cancel();
    try {
      if (!liveDiagnostics) return;
      // Waiting for the watchdog's poll in flight, then running another, could
      // hold quit for 20 seconds. Its last reading is at most a poll old, and
      // anything AXRB itself is still starting keeps the server.
      const watched = emulatorWatchdog?.snapshot().phase;
      emulatorWatchdog?.stop();
      if (runtime.bridgeStopEvent) await runtime.stop().catch(error => liveDiagnostics.append('launcher', `Quest Bridge shutdown failed: ${message(error)}`, { tag: 'runtime', level: 'E' }));
      const idle = !busy && !setup?.status?.active && await runtime.adbServerIdle(watched).catch(() => false);
      liveDiagnostics.append('launcher', idle ? 'Stopping the ADB server; Android is not running.' : 'Leaving the ADB server running for Android or a game session.', { tag: 'runtime' });
      // Capture stops first so its logcat children end quietly with the launcher.
      await liveDiagnostics.stop();
      if (idle) await runtime.stopAdbServer();
    } finally {
      if (installUpdateOnQuit) launcherUpdates.finishInstall();
      else app.quit();
    }
  },
});
const controllers = new Map();
const searchResults = new Map();
let artworkTask;
function refreshArtwork() {
  if (artworkTask) return artworkTask;
  artworkTask = loadLibraryArtwork(state.data.games, new QuestStore(), async (id, artwork) => {
    const game = state.data.games.find(g => g.id === id);
    if (game) { Object.assign(game, artwork); await persist(); }
  }).finally(() => { artworkTask = null; });
  return artworkTask;
}
const uiPath = path.join(directory, 'dist/index.html');
const message = error => String(error?.message || error).replace(/(?:OC|FRL|EA)[A-Za-z0-9_|-]{30,}/g, '[redacted]').replace(/access_token=[^\s&]+/g, 'access_token=[redacted]');
const exists = async file => { try { await fs.access(file); return true; } catch { return false; } };
function publicState() {
  return { ...state.data, setup: setup?.status, signedIn: Boolean(token), account, running: runtime.game, busy, portable: Boolean(portable),
    emulator: emulatorWatchdog?.snapshot() ?? null,
    // Credentials and signed CDN URLs never reach the renderer or library file.
    games: state.data.games.map(g => {
      const { status, label, summary, verifiedVersions } = resolveCompatibility(compatibilityProfiles, g);
      return { ...g, compatibility: { status, label, summary, verifiedVersions },
        files: g.files?.map(f => ({ name: f.name, path: f.path, kind: f.kind, size: f.size })) };
    }) };
}
function changed() { if (window && !window.isDestroyed()) window.webContents.send('axrb:changed', publicState()); }
async function persist() { await state.save(); changed(); }
function getGame(id) { const game = state.data.games.find(g => g.id === id); if (!game) throw new Error('Game is no longer in your library.'); return game; }
function store() { if (!token) throw new Error('Sign in to Meta first.'); return new QuestStore(token); }
async function configuredCli() {
  const cli = state.data.settings.ovrportCli;
  if (!cli || !await exists(cli)) throw new Error('Choose the ovrport CLI executable or JAR in Settings first.');
  return portableOutput(portable, cli);
}
function handler(name, callback) {
  ipcMain.handle(`axrb:${name}`, async (event, ...args) => {
    if (event.sender !== window.webContents || event.senderFrame !== window.webContents.mainFrame) throw new Error('Untrusted launcher request.');
    const started = Date.now(), trace = !name.startsWith('ai') && !['state', 'diagnosticsRead', 'diagnostics', 'copyText'].includes(name);
    if (trace) liveDiagnostics?.append('launcher', `${name} started`, { tag: 'operation' });
    try {
      if (setup && setup.status.phase !== 'ready' && ['play', 'install', 'uninstall', 'import', 'patch', 'settings', 'questDevices', 'questGames', 'questImport', 'importZip', 'startAndroid', 'stopAndroid'].includes(name)) throw new Error('Complete runtime setup first.');
      const value = await callback(...args);
      if (trace) liveDiagnostics?.append('launcher', `${name} completed (${Date.now() - started} ms)`, { tag: 'operation' });
      return { ok: true, value };
    } catch (error) {
      const text = message(error);
      if (trace) liveDiagnostics?.append('launcher', `${name} failed: ${text}`, { level: 'E', tag: 'operation' });
      // A message rewritten for the player keeps the tool's original text as
      // its cause; diagnostics carry both so nothing the tool said is lost.
      if (trace && error?.cause) liveDiagnostics?.append('launcher', `${name} failure detail: ${message(error.cause)}`, { level: 'E', tag: 'operation' });
      return { ok: false, error: text };
    }
  });
}
async function listAdbProcesses() {
  if (process.platform !== 'win32') return null;
  try {
    const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      "Get-CimInstance Win32_Process -Filter \"Name='adb.exe'\" | ForEach-Object { \"$($_.ProcessId) $($_.ExecutablePath) $($_.CommandLine)\" }"], { timeout: 10000 });
    return output.trim() || '(no adb.exe processes)';
  } catch (error) { return `(unavailable: ${message(error)})`; }
}
async function exclusive(callback) { if (busy) throw new Error('Wait for the current install or patch to finish.'); busy = true; changed(); try { return await callback(); } finally { busy = false; changed(); } }

async function syncInstalled() {
  if (busy) return false;
  const installed = await runtime.installed();
  if (!installed || busy) return false;
  state.data.games = state.data.games.filter(g => !(g.source === 'installed' && g.package?.startsWith('com.axrb.')));
  for (const game of state.data.games) game.installed = Boolean(game.package && installed.has(game.package));
  for (const pkg of installed) {
    if (pkg.startsWith('com.axrb.') || pkg.startsWith('com.google.') || state.data.games.some(g => g.package === pkg)) continue;
    try {
      const game = await runtime.importInstalled(pkg, path.join(state.directory, 'icons', `${pkg}.png`));
      if (game) state.put(game);
    } catch { /* Non-launchable packages remain outside the games library. */ }
  }
  await persist(); return true;
}
async function syncMeta() {
  const result = await store().library();
  for (const game of state.data.games) if (game.source === 'meta') game.owned = false;
  for (const game of result.games) state.put(game);
  account = result.name; await persist();
  refreshArtwork().catch(() => {});
  return { partial: result.partial, count: result.games.length };
}
async function login() {
  if (authWindow && !authWindow.isDestroyed()) { authWindow.focus(); return; }
  const auth = new MetaAuth();
  const url = await auth.begin();
  return new Promise((resolve, reject) => {
    const partition = `axrb-meta-${randomUUID()}`;
    const authSession = session.fromPartition(partition);
    authSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
    authWindow = new BrowserWindow({ width: 560, height: 800, title: 'Sign in to Meta', parent: window,
      autoHideMenuBar: true, webPreferences: { partition, sandbox: true, contextIsolation: true, nodeIntegration: false } });
    let completed = false, processing = false;
    const navigate = async (event, target) => {
      let dest; try { dest = new URL(target); } catch { event?.preventDefault(); return; }
      if (['oculus:', 'oculus-client:'].includes(dest.protocol)) {
        event?.preventDefault(); if (processing) return; processing = true;
        try {
          const value = await auth.complete(target);
          await metaSession.save(value);
          token = value; completed = true; authWindow.close(); changed(); resolve();
        } catch (error) { completed = true; authWindow.close(); reject(error); }
      } else if (dest.protocol !== 'https:' || !['meta.com', 'facebook.com', 'oculus.com', 'instagram.com'].some(d => dest.hostname === d || dest.hostname.endsWith(`.${d}`))) event?.preventDefault();
    };
    authWindow.webContents.on('will-navigate', navigate);
    authWindow.webContents.on('will-redirect', navigate);
    authWindow.webContents.setWindowOpenHandler(({ url: target }) => { navigate({ preventDefault() {} }, target); const u = new URL(target); if (u.protocol === 'https:' && ['meta.com', 'facebook.com', 'oculus.com', 'instagram.com'].some(d => u.hostname === d || u.hostname.endsWith(`.${d}`))) authWindow?.loadURL(target); return { action: 'deny' }; });
    authWindow.on('closed', () => { authWindow = null; if (!completed) reject(new Error('Sign-in was cancelled.')); });
    authWindow.loadURL(url).catch(() => { if (!completed) { completed = true; authWindow?.close(); reject(new Error('Could not load Meta sign-in.')); } });
  });
}

async function downloadGame(id, binaryId, dlcId) {
  if (busy) throw new Error('Wait for the current import or installation to finish.');
  const game = getGame(id);
  if (state.data.jobs.some(j => j.gameId === id && ['queued', 'downloading', 'installing', 'patching'].includes(j.status))) throw new Error('This game already has an active task.');
  const api = store();
  const job = { id: randomUUID(), gameId: id, name: game.name, status: 'queued', completed: 0, total: 0, binaryId, dlcId, stage: 'Checking Quest build' };
  state.data.jobs.unshift(job); state.data.jobs = state.data.jobs.slice(0, 50);
  const controller = new AbortController(); controllers.set(job.id, controller); await persist();
  (async () => {
    try {
      const plan = dlcId ? { package: game.package, binaryId: game.binaryId, version: game.version, files: [] } : await api.plan(id, binaryId);
      if (dlcId) {
        if (!game.package) throw new Error('Download the base game before its add-ons.');
        const dlc = (await api.dlc(id)).find(d => d.id === dlcId);
        if (!dlc?.owned) throw new Error('Meta did not confirm ownership of this add-on.');
        if (!dlc.files.length) throw new Error('This add-on has no separately downloadable files; it may be included in the base game.');
        plan.files = dlc.files;
      }
      const target = portableOutput(portable, path.join(state.data.settings.downloadDir, appId(id), appId(plan.binaryId)));
      for (const file of plan.files) safeName(file.name);
      await checkSpace(target, plan.files.reduce((n, f) => n + Number(f.size || 0), 0));
      job.total = plan.files.reduce((n, f) => n + Number(f.size || 0), 0); job.status = 'downloading';
      await persist();
      const files = [];
      let completed = 0, lastUpdate = 0;
      for (const file of plan.files) {
        controller.signal.throwIfAborted();
        job.stage = file.name;
        const destination = portableOutput(portable, path.join(target, safeName(file.name)));
        const result = await downloadFile({ url: await api.downloadUrl(file), destination, size: Number(file.size || 0), signal: controller.signal,
          progress: (bytes, total) => { job.completed = completed + bytes; if (!job.total) job.currentTotal = total;
            if (Date.now() - lastUpdate > 200) { changed(); lastUpdate = Date.now(); } } });
        completed += result.bytes;
        files.push({ name: file.name, path: destination, kind: file.kind, size: result.bytes, sha256: result.sha256 });
      }
      if (!dlcId) {
        const apk = files.find(f => f.kind === 'apk');
        const metadata = await runtime.inspect(apk.path);
        if (metadata.package !== plan.package) throw new Error('Downloaded APK package does not match the selected build.');
        Object.assign(game, { package: metadata.package, activity: metadata.activity, apk: apk.path, patched: metadata.patched,
          version: metadata.version, versionCode: metadata.versionCode, binaryId: plan.binaryId, files, downloaded: true });
      } else {
        game.files = [...(game.files || []).filter(f => !files.some(n => n.name === f.name)), ...files];
      }
      state.put(game);
      job.completed = completed; job.total = completed; job.status = 'complete'; job.stage = dlcId ? 'Add-on downloaded' : 'Ready to install';
    } catch (error) { job.status = controller.signal.aborted ? 'cancelled' : 'failed'; job.error = message(error); }
    finally { controllers.delete(job.id); await persist(); }
  })();
  return job.id;
}

async function importTransfer(kind, input) {
  if (busy || controllers.size) throw new Error('Wait for current transfers and installations to finish.');
  if (kind === 'zip' && runtime.child) throw new Error('Close the running game before installing.');
  busy = true;
  const job = { id: randomUUID(), kind, gameId: `local:${input.package || 'zip'}`, name: input.package || path.basename(input.file),
    status: kind === 'apk' ? 'importing' : 'downloading', stage: kind === 'apk' ? 'Reading APK' : 'Preparing import', completed: 0, total: 0 };
  const controller = new AbortController(); controllers.set(job.id, controller);
  state.data.jobs.unshift(job);
  try { await persist(); } catch (error) { busy = false; controllers.delete(job.id); throw error; }
  void (async () => {
    let last = 0;
    const notify = () => { if (Date.now() - last > 150) { last = Date.now(); changed(); } };
    const options = { signal: controller.signal,
      update: stage => { job.stage = stage; notify(); },
      progress: (completed, total) => { job.completed = completed; job.total = total; job.progressUnit = kind === 'zip' ? 'files' : 'bytes'; notify(); } };
    try {
      const inspect = (file, flags) => runtime.inspect(file, flags);
      portableOutput(portable, state.data.settings.downloadDir);
      const imported = kind === 'quest'
        ? await new Quest(runtime.settings).pullGame(input.serial, input.package, state.data.settings.downloadDir, inspect, options)
        : kind === 'apk' ? { ...await inspect(input.file), apk: (await carryPortableFiles(portable, state.data.settings.downloadDir, [input.file], controller.signal))[0], downloaded: true }
        : await importGameZip(input.file, state.data.settings.downloadDir, inspect, options);
      controller.signal.throwIfAborted();
      const existing = state.data.games.find(g => g.package === imported.package);
      const game = state.put({ ...imported, id: existing?.id || imported.id, source: existing?.source || imported.source,
        importedFrom: kind, installed: existing?.installed || false });
      job.gameId = game.id; job.name = game.name; job.completed = job.total;
      await persist();
      if (kind === 'zip') {
        Object.assign(job, { status: 'installing', stage: 'Starting Android', completed: 0, total: 0, progressUnit: 'bytes' }); changed();
        // A cancelled extraction never reaches installation. After this point the
        // job remains an install, so its APK/asset transaction is not interrupted.
        await runtime.install(game, (stage, progress) => { Object.assign(job, { stage, completed: 0, total: 0, progressUnit: 'bytes' }, progress); changed(); });
        game.installed = true;
      }
      job.status = 'complete'; job.stage = kind === 'zip' ? 'Installed' : 'Ready to install';
    } catch (error) { job.status = controller.signal.aborted ? 'cancelled' : 'failed'; job.error = message(error); }
    finally { controllers.delete(job.id); busy = false; await persist(); }
  })().catch(error => { console.error(message(error)); });
  return job.id;
}

async function bootstrap() {
compatibilityProfiles = await loadCompatibilityProfiles(path.join(directory, 'core/game-compatibility.json'));
state = new State(app.getPath('userData')); await state.load({ portableRoot: portable });
metaSession = new MetaSession(state.directory, { portable: Boolean(portable), safeStorage });
const pendingRuntime = await state.pendingRuntime();
const components = JSON.parse(await fs.readFile(path.join(directory, 'core/components.json'), 'utf8'));
state.data.settings = { sdk: path.join(process.env.LOCALAPPDATA || '', 'Android/Sdk'), avd: 'axrb-games-api34', port: 5580,
  memoryMB: 8192, cpuCores: 4, downloadDir: path.join(portable || app.getPath('downloads'), portable ? 'downloads' : 'AXRB'), ovrportCli: '',
  precomposeProjectionLayers: false,
  guestClock: 'Auto', ...state.data.settings };
migrateClockPolicy(state.data.settings);
await state.save();
portableOutput(portable, state.data.settings.downloadDir);
runtime = new Runtime(root, state.data.settings, (text, metadata) => liveDiagnostics?.write('launcher', text, { tag: 'runtime', ...metadata }));
if (!smoke && (app.isPackaged || pendingRuntime || state.data.settings.managedDirectory || !await exists(path.join(state.data.settings.sdk, 'emulator/emulator.exe')))) {
  const managed = pendingRuntime?.directory || state.data.settings.managedDirectory || path.join(portable || process.env.LOCALAPPDATA, 'AXRB Runtime');
  portableOutput(portable, managed);
  // The setup receipt persists the managed root; derive all runtime paths from
  // it on every launch so a previous install never falls back to the user's
  // unrelated default SDK, AVD or emulator port.
  Object.assign(runtime.settings, { sdk: path.join(managed, 'sdk'), avd: 'axrb-managed-api36', port: 5584 });
  setup = new Setup({ root, portableRoot: portable, directory: managed, currentDirectory: state.data.settings.managedDirectory || managed,
    resumableDirectory: pendingRuntime?.directory || '', runtime, components,
    select: value => state.stageRuntime(portableOutput(portable, value)),
    stage: (value, installed) => state.stageRuntime(value, installed),
    save: async (value, installed) => {
      await state.commitRuntime(value, runtime.settings, installed);
      changed();
    }, changed, debug,
    onOutput: (text, metadata) => liveDiagnostics?.write('launcher', text, { tag: 'setup', ...metadata }) });
  setup.environment();
  if (pendingRuntime?.installed && (await setup.inspect().catch(() => null))?.ready) {
    await state.commitRuntime(managed, runtime.settings, new Set(pendingRuntime.installed));
    setup.currentDirectory = managed;
  }
}
liveDiagnostics = new LiveDiagnostics({
  directory: path.join(state.directory, 'diagnostics'),
  getConfig: () => ({ sdk: runtime.settings.sdk, avd: runtime.settings.avd, port: runtime.settings.port, dataHome: process.env.AXRB_DATA_HOME || path.join(root, 'out') }),
  // Setup owns ADB while it installs or boots Android, and every operation
  // behind the exclusive gate drives it too. Capture keeps watching, but
  // leaves starting the server to them rather than forking a competitor.
  adbBusy: () => busy || Boolean(setup?.status?.active),
  onUpdate: () => { if (window && !window.isDestroyed()) window.webContents.send('axrb:diagnostics'); },
});
await liveDiagnostics.start();
emulatorWatchdog = new EmulatorWatchdog({
  getStatus: () => runtime.status(),
  onChange: () => changed(),
  onTransition: state => liveDiagnostics.append('launcher', state.detail, { tag: 'watchdog', level: state.phase === 'unknown' ? 'W' : 'I' }),
});
emulatorWatchdog.start();
// Reclaiming the drive must never delay or fail startup, and anything a live
// run is still using is far newer than the cutoff.
if (portable) void sweepPortableTemp(portable)
  .then(removed => { if (removed) liveDiagnostics?.write('launcher', `Removed ${removed} stale portable temporary ${removed === 1 ? 'entry' : 'entries'}.\n`, { tag: 'runtime' }); })
  .catch(error => liveDiagnostics?.write('launcher', `Portable temporary sweep failed: ${message(error)}\n`, { tag: 'runtime' }));
token = await metaSession.load();
window = new BrowserWindow({ width: 1320, height: 880, minWidth: 920, minHeight: 640, title: 'AXRB', icon: path.join(directory, 'assets/axrb.ico'), backgroundColor: '#141414',
  autoHideMenuBar: true, webPreferences: { preload: path.join(directory, 'preload.cjs'), sandbox: true, contextIsolation: true, nodeIntegration: false } });
window.on('close', event => {
  if (quitting) return;
  event.preventDefault();
  void requestClose();
});
window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
window.webContents.on('will-navigate', event => event.preventDefault());
window.webContents.session.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
handler('state', () => publicState());
handler('diagnosticsRead', (afterId = 0, afterEviction = null) => {
  if (!Number.isSafeInteger(afterId) || afterId < 0) throw new Error('Invalid diagnostic cursor.');
  if (afterEviction !== null && (!Number.isSafeInteger(afterEviction) || afterEviction < 0)) throw new Error('Invalid diagnostic cursor.');
  // A reader without an eviction cursor starts from a full snapshot.
  return liveDiagnostics.snapshot(afterId, afterEviction ?? -1);
});
handler('copyText', text => {
  if (typeof text !== 'string' || Buffer.byteLength(text, 'utf8') > 4 * 1024 * 1024) throw new Error('Clipboard text is too large.');
  return clipboard.writeText(text);
});
handler('setupCheck', () => setup?.check());
handler('setupStart', options => setup?.start(options));
handler('setupCancel', () => setup?.cancel());
handler('chooseSetupArchives', async () => {
  if (setup?.status.active) throw new Error('Wait until setup is ready to select archives.');
  const choice = await dialog.showOpenDialog(window, { title: 'Use downloaded Android setup files',
    properties: ['openFile', 'multiSelections'], filters: [{ name: 'Android setup archives', extensions: ['zip'] }] });
  if (choice.canceled) return null;
  if (choice.filePaths.length > components.length) throw new Error(`Select at most ${components.length} Android setup archives.`);
  return identifyArchives(choice.filePaths, components);
});
handler('setupLicense', async () => { const error = await shell.openPath(path.join(app.isPackaged ? process.resourcesPath : directory, 'licenses/android-sdk.txt')); if (error) throw new Error(error); });
handler('login', async () => { await login(); return syncMeta(); });
handler('logout', async () => {
  for (const controller of controllers.values()) controller.abort();
  token = ''; account = '';
  await metaSession.clear();
  changed();
});
handler('sync', async () => { const online = await syncInstalled(); const result = token ? await syncMeta() : null; return { online, meta: result }; });
handler('search', async text => { const games = await new QuestStore(token).search(text); for (const game of games) searchResults.set(game.id, game); return games; });
handler('add', async id => { const game = searchResults.get(appId(id)); if (!game) throw new Error('Search for this app again.'); const existing = state.data.games.find(g => g.id === game.id); if (!existing) state.put(game); await persist(); return game.id; });
handler('lookup', async input => { const game = await store().details(appId(input)); state.put(game); await persist(); return game.id; });
handler('builds', id => store().builds(appId(id)).then(items => items.map(b => ({ id: String(b.id), version: b.version, code: b.version_code ?? b.versionCode }))));
handler('download', (id, binaryId) => downloadGame(appId(id), binaryId));
handler('dlc', id => store().dlc(appId(id)).then(items => items.map(({ files, ...item }) => ({ ...item, fileCount: files.length, bytes: files.reduce((n, f) => n + f.size, 0) }))));
handler('downloadDlc', (id, dlcId) => downloadGame(appId(id), null, appId(dlcId)));
handler('cancel', id => { if (['queued', 'downloading', 'importing'].includes(state.data.jobs.find(j => j.id === id)?.status)) controllers.get(id)?.abort(); });
handler('retry', id => { const job = state.data.jobs.find(j => j.id === id); if (!job || !['failed', 'interrupted', 'cancelled'].includes(job.status)) throw new Error('This task cannot be retried.'); return downloadGame(job.gameId, job.binaryId, job.dlcId); });
handler('import', async () => {
  if (busy) throw new Error('Wait for the current task to finish.');
  const result = await dialog.showOpenDialog(window, { title: 'Import an Android game', filters: [{ name: 'Android APK', extensions: ['apk'] }], properties: ['openFile'] });
  if (result.canceled) return;
  return importTransfer('apk', { file: result.filePaths[0] });
});
handler('questDevices', () => new Quest(runtime.settings).devices());
handler('questGames', async serial => (await new Quest(runtime.settings).games(serial)).map(game => {
  const known = state.data.games.find(g => g.package === game.package);
  return { ...game, name: known?.name || game.name, downloaded: Boolean(known?.downloaded), image: known?.image || '' };
}));
handler('questImport', (serial, packageName) => importTransfer('quest', { serial, package: packageName }));
handler('importZip', async () => {
  if (busy) throw new Error('Wait for the current task to finish.');
  const result = await dialog.showOpenDialog(window, { title: 'Install game ZIP', filters: [{ name: 'Game ZIP', extensions: ['zip'] }], properties: ['openFile'] });
  if (!result.canceled) return importTransfer('zip', { file: result.filePaths[0] });
});
handler('importAssets', id => exclusive(async () => {
  const game = getGame(id);
  const result = await dialog.showOpenDialog(window, { title: 'Add expansion files / DLC assets', properties: ['openFile', 'multiSelections'] });
  if (result.canceled) return;
  const files = [];
  // Android installs expansion files by name, so two selections sharing one is
  // a mistake to report rather than an ambiguous pair of library entries.
  const chosen = result.filePaths.map(file => path.basename(file));
  const duplicate = chosen.find((name, index) => chosen.indexOf(name) !== index);
  if (duplicate) throw new Error(`Selected two files named "${duplicate}". Add one of them at a time.`);
  const carried = await carryPortableFiles(portable, state.data.settings.downloadDir, result.filePaths, shutdown.signal);
  for (const file of carried) files.push({ path: file, name: safeName(path.basename(file)), kind: file.endsWith('.obb') ? 'obb' : 'asset', size: (await fs.stat(file)).size });
  game.files = [...(game.files || []).filter(f => !files.some(n => n.name === f.name)), ...files]; await persist();
}));
handler('install', id => exclusive(async () => {
  const game = getGame(id);
  // Verify downloaded artifacts before any installation; imported APKs remain user-managed.
  for (const file of game.files || []) if (file.sha256) {
    const hash = createHash('sha256'), handle = await fs.open(file.path);
    try { for await (const chunk of handle.createReadStream()) hash.update(chunk); } finally { await handle.close(); }
    if (hash.digest('hex') !== file.sha256) throw new Error(`${file.name} changed since download. Download it again.`);
  }
  const job = { id: randomUUID(), gameId: id, name: game.name, status: 'installing', stage: 'Preparing install' }; state.data.jobs.unshift(job); await persist();
  try { await runtime.install(game, (stage, progress) => { Object.assign(job, { stage, completed: 0, total: 0, progressUnit: 'bytes' }, progress); changed(); }); game.installed = true; job.status = 'complete'; job.stage = 'Installed'; }
  catch (error) { job.status = 'failed'; job.error = message(error); throw error; }
  finally { await persist(); }
}));
handler('uninstall', id => exclusive(async () => {
  const game = getGame(id);
  if (!game.installed) throw new Error('Game is not installed.');
  if (runtime.child) throw new Error('Close the running game before uninstalling.');
  // Scoped to this game's own jobs, matching the UI's own activeJob check
  // (game-details.jsx): an unrelated game's download must not block this one.
  if (state.data.jobs.some(j => j.gameId === id && ['queued', 'downloading', 'installing', 'patching', 'importing', 'uninstalling'].includes(j.status))) {
    throw new Error('Finish this game’s transfer before uninstalling.');
  }
  const answer = await dialog.showMessageBox(window, { type: 'warning', title: 'Uninstall game',
    message: `Uninstall ${game.name}?`, detail: 'Removes the game and its saved data from AXRB’s Android emulator. Downloaded APKs and assets stay on your PC.',
    buttons: ['Cancel', 'Uninstall'], defaultId: 0, cancelId: 0, noLink: true });
  if (answer.response !== 1) return false;
  const job = { id: randomUUID(), kind: 'uninstall', gameId: id, name: game.name, status: 'uninstalling', stage: 'Preparing uninstall' };
  state.data.jobs.unshift(job); await persist();
  try {
    await runtime.uninstall(game, stage => { job.stage = stage; changed(); });
    game.installed = false; job.status = 'complete'; job.stage = 'Uninstalled';
  } catch (error) { job.status = 'failed'; job.error = message(error); }
  finally { await persist(); }
  return job.status === 'complete';
}));
handler('patch', (id, selected) => exclusive(async () => {
  const game = getGame(id), cli = await configuredCli();
  if (!game.apk) throw new Error('Download or import the APK first.');
  // Patch from the original APK every time. After a patch game.apk points at
  // the patched copy, and patching that again stacks OVRPort's changes onto
  // its own output (base-axrb-axrb.apk).
  // Older builds kept only the patched copy's path, and that copy may since
  // have been deleted while the original still sits beside where it was.
  const present = await exists(game.apk);
  let input = game.apk, identity = present ? await runtime.inspect(input) : null;
  if (!identity || identity.patched) {
    const original = await findOriginalApk(game, identity ?? { versionCode: game.versionCode }, file => runtime.inspect(file), { searchRoots: [state.data.settings.downloadDir] });
    if (!original) throw new Error(present
      ? 'This APK is already patched and its original is no longer available. Download or import the original APK to patch it again.'
      : "This game's APK is no longer on disk. Download or import it again to patch it.");
    ({ file: input, identity } = original);
  }
  game.sourceApk = input;
  if (identity.package !== game.package) throw new Error('APK package no longer matches this game; import it separately.');
  Object.assign(game, { version: identity.version, versionCode: identity.versionCode, activity: identity.activity, patched: input === game.apk ? identity.patched : true });
  await persist();
  const compatibility = resolveCompatibility(compatibilityProfiles, identity);
  let patchArgs;
  if (compatibility.status === 'matched') {
    if (selected !== undefined) throw new Error('This game now has a verified compatibility profile. Reopen its details to patch.');
    patchArgs = compatibilityPatchArgs(compatibility);
    if (patchArgs.length) await ovrport.requireProfiles(cli, compatibility.profile.ovrport?.extraPatches ?? []);
  } else {
    if (selected === undefined) return { patches: await ovrport.patches(cli) };
    patchArgs = selectedPatchArgs(await ovrport.patches(cli), selected);
  }
  const outputDirectory = portableOutput(portable, portable
    ? path.join(state.data.settings.downloadDir, 'patched', game.package)
    : path.join(path.dirname(input), 'axrb-patched'));
  const output = portableOutput(portable, path.join(outputDirectory, `${path.basename(input, path.extname(input))}-axrb.apk`));
  // A re-patch produces the same file name as the APK the game uses now, and
  // OVRPort streams straight into its output. Stage it, so a failed or
  // cancelled run leaves the working APK untouched.
  // A run that timed out can leave its staging folder behind while java still
  // holds the file, and a quit skips the cleanup below. Patches never overlap,
  // so any staging folder left over now is dead.
  for (const entry of await fs.readdir(outputDirectory).catch(() => [])) {
    if (entry.startsWith('.staging-')) await fs.rm(path.join(outputDirectory, entry), { recursive: true, force: true }).catch(() => {});
  }
  const staging = path.join(outputDirectory, `.staging-${randomUUID().slice(0, 8)}`);
  const args = ['patch', `--input=${input}`, `--output=${staging}`, '--output-name={filename}-axrb.apk', ...patchArgs];
  try {
    await fs.mkdir(staging, { recursive: true });
    await ovrport.run(cli, args, { timeout: 20 * 60 * 1000 });
    const staged = path.join(staging, path.basename(output));
    const metadata = await runtime.inspect(staged);
    if (metadata.package !== game.package) throw new Error('Patched APK changed its package name; import it separately.');
    await fs.rename(staged, output);
    Object.assign(game, { apk: output, patched: true, version: metadata.version, versionCode: metadata.versionCode, activity: metadata.activity });
  } finally { await fs.rm(staging, { recursive: true, force: true }).catch(() => {}); }
  await persist();
  return { profileLabel: compatibility.status === 'matched' ? compatibility.label : null };
}));
handler('play', (id, backend = 'android') => exclusive(async () => {
  const game = getGame(id);
  if (!['android', 'quest-bridge'].includes(backend)) throw new Error('Unknown runtime backend.');
  if (backend === 'android' && !game.installed) throw new Error('Install the game first.');
  if (runtime.child) throw new Error('A game is already running.');
  const bridge = backend === 'quest-bridge' ? await prepareBridge({ root, directory: path.join(state.directory, 'quest-bridge'), sdk: runtime.settings.sdk, game, execute: run, onOutput: runtime.onOutput }) : null;
  const prepared = bridge ? { game: { ...game, versionCode: bridge.versionCode }, ownsEmulator: false } : await runtime.prepareLaunch(game);
  let compatibility;
  try {
    compatibility = resolveCompatibility(bridge ? [] : compatibilityProfiles, prepared.game);
    liveDiagnostics.append('launcher', `Launching ${game.package} with ${backend}`, { tag: 'game' });
    const sessionId = randomUUID().slice(0, 8);
    const sessionStartedAt = new Date().toISOString();
    liveDiagnostics.resetPerf();
    liveDiagnostics.append('launcher', `session ${sessionId} started: package=${prepared.game.package}${prepared.game.activity ? ` activity=${prepared.game.activity}` : ''}`, { tag: 'session' });
    runtime.launch(prepared.game, async (code, tail) => {
      liveDiagnostics.append('launcher', `Game process exited (${code ?? 'unknown'}).`, { tag: 'game', level: code ? 'E' : 'I' });
      // Merge the run script's structured record when present; its exit code
      // and flags are authoritative over any transcript text.
      let record = { id: sessionId, backend, package: prepared.game.package, startedAt: sessionStartedAt, endedAt: new Date().toISOString(), exitCode: code ?? null };
      try {
        const parsed = parseSessionRecord(await fs.readFile(sessionRecordPath(), 'utf8'), sessionId);
        // A script that failed early writes nulls; those must not erase the
        // launcher's own start time or package.
        const present = Object.fromEntries(Object.entries(parsed ?? {}).filter(([, value]) => value !== null));
        if (parsed) record = { ...record, ...present, id: sessionId, exitCode: code ?? null };
      } catch { /* No record means an older script or a failed spawn; the fields above still ship. */ }
      sessionRecords.unshift(JSON.stringify(record));
      if (sessionRecords.length > 20) sessionRecords.length = 20;
      const ending = describeSessionEnd(record, code);
      const save = record.pauseSucceeded && record.syncSucceeded ? 'saved' : 'not confirmed';
      liveDiagnostics.append('launcher', `session ${sessionId} ended: ${ending.outcome}; save ${save}`, { tag: 'session', level: code ? 'E' : 'I' });
      if (code) {
        const error = message(new Error(ending.detail || tail || `Game launcher exited with code ${code}.`));
        state.data.jobs.unshift({ id: randomUUID(), gameId: id, name: game.name, status: 'failed', stage: 'Launch', error });
        if (window && !window.isDestroyed()) window.webContents.send('axrb:launch-error', `${game.name}: ${error}`);
      }
      await persist();
    }, compatibilityRuntimeOptions(compatibility), { ownsEmulator: prepared.ownsEmulator, sessionId, bridge });
  } catch (error) {
    if (prepared.ownsEmulator) await runtime.adb(['emu', 'kill']).catch(cleanupError => liveDiagnostics.append('launcher', `Android shutdown failed: ${message(cleanupError)}`, { tag: 'game', level: 'W' }));
    throw error;
  }
  game.lastPlayed = new Date().toISOString(); await persist();
  return {
    profileLabel: compatibility.status === 'matched' ? compatibility.label : null,
    compatibilityNotice: compatibility.status === 'mismatch' ? `Installed Android build: ${compatibility.summary}` : null
  };
}));
handler('stop', () => runtime.stop());
// Manual controls alongside the automated boot/shutdown that already happens
// around play, install and uninstall. Reuses the same exclusive() gate those
// share, since a manual boot or shutdown is exactly as disruptive to them.
handler('startAndroid', () => exclusive(() => runtime.ensure()));
handler('stopAndroid', () => exclusive(async () => {
  if (runtime.child) throw new Error('Close the running game before stopping Android.');
  if (!await runtime.online()) return;
  await runtime.adb(['emu', 'kill']);
}));
handler('fpsHud', async enabled => {
  await runtime.setFpsHud(enabled);
  state.data.settings.fpsHud = enabled;
  await persist();
});
handler('settings', async values => {
  const allowed = ['sdk', 'avd', 'port', 'memoryMB', 'cpuCores', 'downloadDir', 'ovrportCli', 'diagnosticsEndpoint', 'precomposeProjectionLayers'];
  if (!values || typeof values !== 'object') throw new Error('Invalid settings.');
  if (busy || controllers.size || runtime.child) throw new Error('Finish current tasks before changing runtime settings.');
  const settings = { ...state.data.settings };
  for (const key of allowed) if (values[key] !== undefined) settings[key] = values[key];
  if (settings.managedDirectory && (settings.sdk !== state.data.settings.sdk || settings.avd !== state.data.settings.avd)) throw new Error('Managed Android paths cannot be changed here.');
  if (!/^[A-Za-z0-9_-]+$/.test(settings.avd) || !Number.isInteger(settings.port) || settings.port < 5554 || settings.port > 5682 || settings.port % 2 ||
    !Number.isInteger(settings.memoryMB) || settings.memoryMB < 2048 || settings.memoryMB > 16384) throw new Error('Check the Android AVD, even-numbered port, and memory settings.');
  // Six is the Android emulator's own ceiling: it clamps -cores above that and
  // reports the clamped count back, which ensure() would then read as a
  // mismatch and refuse to launch. Never offer a number it will not honour.
  if (!Number.isInteger(settings.cpuCores) || settings.cpuCores < 2 || settings.cpuCores > 6) throw new Error('Choose between 2 and 6 vCPUs.');
  if (typeof settings.precomposeProjectionLayers !== 'boolean') throw new Error('Choose whether to precompose projection layers.');
  for (const key of ['sdk', 'downloadDir']) if (typeof settings[key] !== 'string' || !path.isAbsolute(settings[key])) throw new Error('Select absolute Windows paths.');
  portableOutput(portable, settings.downloadDir);
  if (settings.ovrportCli) portableOutput(portable, settings.ovrportCli);
  // Empty keeps the default paste service; anything else must be a self-hosted
  // HTTPS endpoint, so logs cannot be redirected to a plaintext collector.
  if (settings.diagnosticsEndpoint) {
    let endpoint; try { endpoint = new URL(settings.diagnosticsEndpoint); } catch { throw new Error('The diagnostics endpoint must be a full https:// URL.'); }
    if (endpoint.protocol !== 'https:') throw new Error('The diagnostics endpoint must use https://.');
  }
  state.data.settings = settings; runtime.settings = settings; await persist();
});
// Android's own permission dialog is unreachable while the emulator runs
// headless, so the grant is made from here instead.
handler('permissions', async id => {
  const game = getGame(id);
  if (!await runtime.online()) throw new Error('Start Android first: permissions live on the virtual device.');
  return runtime.permissions(game.package);
});
handler('setPermission', async (id, permission, granted) => {
  const game = getGame(id);
  if (!await runtime.online()) throw new Error('Start Android first: permissions live on the virtual device.');
  return runtime.setPermission(game.package, permission, granted);
});
// Resizing rewrites the AVD's disk geometry, so Android has to be stopped and
// its quick-boot snapshot discarded; the emulator then grows the partition on
// the next cold boot.
handler('storage', async storageGB => {
  if (runtime.child) throw new Error('Close the running game before changing Android storage.');
  if (await runtime.online()) throw new Error('Stop Android before changing its storage size.');
  const directory = avdDirectory(runtime.settings), configFile = path.join(directory, 'config.ini');
  let configText;
  try { configText = await fs.readFile(configFile, 'utf8'); }
  catch { throw new Error('That virtual device has no configuration yet. Finish runtime setup first.'); }
  const currentGB = parseStorageGB(configText);
  if (!planStorageChange(currentGB, storageGB)) return { storageGB, previousGB: currentGB, changed: false };
  await fs.writeFile(configFile, withStorageGB(configText, storageGB));
  await fs.rm(path.join(directory, 'snapshots/default_boot'), { recursive: true, force: true });
  const settings = { ...state.data.settings, storageGB };
  state.data.settings = settings; runtime.settings = settings;
  if (setup) setup.status.storageGB = storageGB;
  await persist();
  return { storageGB, previousGB: currentGB, changed: true };
});
handler('chooseFolder', async () => {
  const choice = await dialog.showOpenDialog(window, { defaultPath: portable || undefined, properties: ['openDirectory', 'createDirectory'] });
  return choice.canceled ? null : portableOutput(portable, choice.filePaths[0]);
});
handler('chooseCli', async () => { const choice = await dialog.showOpenDialog(window, { defaultPath: portable || undefined, properties: ['openFile'], filters: [{ name: 'ovrport CLI', extensions: ['exe', 'jar'] }] }); return choice.canceled ? null : portableOutput(portable, choice.filePaths[0]); });
handler('openFolder', async id => { const game = getGame(id); const target = portableOutput(portable, game.apk ? path.dirname(game.apk) : state.data.settings.downloadDir); await fs.mkdir(target, { recursive: true }); const error = await shell.openPath(target); if (error) throw new Error(error); });
handler('openStore', async id => shell.openExternal(id ? `https://www.meta.com/experiences/${appId(id)}/` : 'https://www.meta.com/experiences/'));
// Uploading publishes the logs, so this only ever runs from an explicit click,
// and the bundle is offered for review before it leaves the machine.
async function diagnosticSnapshot({ compact = false } = {}) {
  return collectDiagnostics({
    dataHome: process.env.AXRB_DATA_HOME || path.join(root, 'out'),
    version: app.getVersion(), settings: state.data.settings,
    setupLogs: setup?.status.logs ?? [], hardware: setup?.status.hardware ?? null,
    liveLogs: compact ? liveDiagnostics.text().slice(-40000) : liveDiagnostics.text(),
    adbProcesses: await listAdbProcesses(),
    sessions: sessionRecords,
    perf: liveDiagnostics.perfText(),
  });
}
handler('diagnostics', async ({ upload = false, save = false } = {}) => {
  if (upload) {
    if (!reviewedDiagnostics) throw new Error('Preview the diagnostics report before uploading it.');
    const bundle = reviewedDiagnostics;
    return { bundle, url: await uploadDiagnostics(bundle, { endpoint: state.data.settings.diagnosticsEndpoint || undefined }) };
  }
  const bundle = await diagnosticSnapshot();
  if (save) {
    const result = await dialog.showSaveDialog(window, {
      title: 'Save diagnostics report', defaultPath: path.join(portable ? path.join(portable, 'data/logs') : '.', `AXRB-diagnostics-${new Date().toISOString().replaceAll(':', '-')}.txt`),
      filters: [{ name: 'Text report', extensions: ['txt'] }],
    });
    if (result.canceled || !result.filePath) return { bundle, path: null };
    await fs.writeFile(portableOutput(portable, result.filePath), bundle, 'utf8');
    return { bundle, path: result.filePath };
  }
  reviewedDiagnostics = bundle;
  return { bundle };
});
chatgpt = new ChatGPTAuth(state.directory, safeStorage, url => shell.openExternal(url));
let aiUpdateTimer;
let aiGameId = '';
const aiHarness = new DiagnosticHarness({
  context: () => {
    const game = state.data.games.find(g => g.id === (aiGameId || runtime.game));
    return { sdk: runtime.settings.sdk, port: runtime.settings.port, avd: runtime.settings.avd,
      root, launcher:directory, downloadDir:runtime.settings.downloadDir, avdHome:process.env.ANDROID_AVD_HOME,
      games:state.data.games.map(g=>({name:g.title || g.name || g.package,package:g.package,installed:Boolean(g.installed)})),
      dataHome: process.env.AXRB_DATA_HOME || path.join(root, 'out'), game: game?.id || '', package: game?.package || '',
      title: game?.name || game?.title || '', session: runtime.child?.pid || null, backend: runtime.bridgeStopEvent ? 'quest-bridge' : 'android-emulator',
      runningGame: runtime.game || '', watchdog: emulatorWatchdog?.snapshot(),
      memoryMB: runtime.settings.memoryMB, cpuCores: runtime.settings.cpuCores, hardware: setup?.status.hardware };
  },
  snapshot: () => diagnosticSnapshot({ compact: true }),
  live: () => `${liveDiagnostics.text()}\n${liveDiagnostics.perfText()}`,
  performance: async (seconds, signal) => {
    if (!runtime.child) throw new Error('No running AXRB game. Start the game and keep the headset active before measuring.');
    if (aiGameId && aiGameId !== runtime.game) throw new Error('Selected game is not the running game.');
    const game = state.data.games.find(g => g.id === runtime.game);
    return run('powershell.exe', performanceScanArgs(root, state.data.settings, { seconds, version: app.getVersion(), packageName: game?.package || '' }),
      { timeout: performanceScanTimeout(seconds), signal });
  },
  approveStop: async (reason, signal, assertScope) => {
    if (!runtime.child) return { stopped: false, reason: 'No running game.' };
    if (aiGameId && aiGameId !== runtime.game) throw new Error('Selected game is not the running game.');
    const { response } = await dialog.showMessageBox(window, { type: 'question', title: 'AXRB diagnostic action',
      message: 'Allow the assistant to stop the running game?', detail: `${cleanEvidence(reason)}\n\nUnsaved game progress may be lost.`,
      buttons: ['Keep running', 'Stop game'], defaultId: 0, cancelId: 0, noLink: true, signal });
    signal.throwIfAborted(); assertScope();
    if (response !== 1) return { stopped: false, reason: 'User declined. Do not request this again in this investigation.' };
    await exclusive(() => runtime.stop());
    return { stopped: true };
  },
  approveAdb: async (args, reason, signal, assertScope) => {
    const { response }=await dialog.showMessageBox(window,{type:'question',title:'AXRB assistant',
      message:'Allow this emulator command?',
      detail:`${reason}\n\nTarget: emulator-${runtime.settings.port} (AXRB only)\nArguments: ${JSON.stringify(args)}\n\nThis may modify Android, installed games, files or runtime state.`,
      buttons:['Cancel','Run command'],defaultId:0,cancelId:0,noLink:true,signal});
    signal.throwIfAborted();assertScope();return response===1;
  },
});
aiDiagnostics = new AiDiagnostics(chatgpt, fetch, () => {
  if (aiUpdateTimer) return;
  aiUpdateTimer = setTimeout(() => {
    aiUpdateTimer = null;
    if (window && !window.isDestroyed()) window.webContents.send('axrb:ai-update', aiDiagnostics.status());
  }, 60);
}, aiHarness);
handler('aiStatus', async () => ({ auth: await chatgpt.status(), ...aiDiagnostics.status(),
  games: state.data.games.filter(g => g.package).map(g => ({ id: g.id, name: g.title || g.name || g.package })) }));
let aiAccountOperation = false;
handler('aiAccount', async ({ action, id } = {}) => {
  if (aiAccountOperation || aiDiagnostics.controller) throw new Error('Finish or stop the current AI operation first.');
  aiAccountOperation = true;
  try {
    if (!['login', 'select', 'logout'].includes(action)) throw new Error('Unknown account action.');
    aiDiagnostics.reset(); aiDiagnostics.models = []; aiDiagnostics.limits = null; aiDiagnostics.cacheKey = `axrb-${randomUUID()}`;
    return action === 'login' ? await chatgpt.login(id) : action === 'select' ? await chatgpt.select(id) : await chatgpt.logout();
  } finally { aiAccountOperation = false; }
});
handler('aiModels', async () => {
  if (aiAccountOperation || chatgpt.pending || aiDiagnostics.controller) throw new Error('Finish the current AI operation first.');
  aiAccountOperation = true;
  try { return await aiDiagnostics.catalog(); } finally { aiAccountOperation = false; }
});
handler('aiCollect', async () => {
  const bundle = cleanEvidence(await diagnosticSnapshot({ compact: true }));
  return { evidence: bundle.slice(0, MAX_EVIDENCE), truncated: bundle.length > MAX_EVIDENCE, collectedAt: new Date().toISOString() };
});
handler('aiAnalyze', input => {
  if (aiAccountOperation || chatgpt.pending) throw new Error('Finish ChatGPT sign-in first.');
  if (aiDiagnostics.controller) throw new Error('An analysis is already running.');
  const selected = input?.gameId || '';
  if (selected && !state.data.games.some(g => g.id === selected && g.package)) throw new Error('Select a game from your library.');
  if (aiGameId !== selected) aiDiagnostics.reset();
  aiGameId = selected;
  return aiDiagnostics.analyze(input);
});
handler('aiCancel', () => { chatgpt.cancel(); aiDiagnostics.cancel(); });
handler('aiReset', () => aiDiagnostics.reset());
handler('aiReport', async ({ text, action } = {}) => {
  text = checkedText(text, 180000, 'Report');
  if (action === 'issue') {
    // User reviews the draft in AXRB, then pastes it into GitHub and submits it.
    // No diagnostics are encoded in a URL or published by the launcher.
    clipboard.writeText(text);
    await shell.openExternal('https://github.com/TheReal-Flo/AXRB-BS/issues/new');
    return { copied: true };
  }
  if (action !== 'save') throw new Error('Unknown report action.');
  const result = await dialog.showSaveDialog(window, { title: 'Save bug report', defaultPath: 'AXRB-bug-report.md', filters: [{ name: 'Markdown', extensions: ['md'] }] });
  if (result.canceled || !result.filePath) return { path: null };
  await fs.writeFile(portableOutput(portable, result.filePath), text, 'utf8');
  return { path: result.filePath };
});
// A scan describes a session that is happening, not one that happened: it
// samples the guest's threads and the bridge's timers while they run. The
// renderer only offers it during a session, and this refuses again in case the
// game exited between the click and the call.
handler('performanceScan', async ({ upload = false, seconds = 10 } = {}) => {
  if (!runtime.child) throw new Error('Start a game first: the scan samples a running session.');
  const game = state.data.games.find(g => g.id === runtime.game);
  const output = await run('powershell.exe', performanceScanArgs(root, state.data.settings,
    { seconds, version: app.getVersion(), packageName: game?.package || '' }), { timeout: performanceScanTimeout(seconds) });
  // The script removes the same three identifiers itself, so that a report
  // someone runs by hand is safe to send. Running it again here costs nothing
  // and keeps the launcher's guarantee independent of the script's.
  const bundle = redact(output.replaceAll('\r\n', '\n')).trim() + '\n';
  if (!upload) return { bundle };
  return { bundle, url: await uploadDiagnostics(bundle, { endpoint: state.data.settings.diagnosticsEndpoint || undefined, title: 'AXRB performance scan' }) };
});
const uiErrors = [];
if (smoke) window.webContents.on('console-message', details => { if (details.level === 'error') uiErrors.push(details.message); });
await window.loadFile(uiPath);
window.show();
window.focus();
if (app.isPackaged && !smoke && !portable && !process.env.PORTABLE_EXECUTABLE_FILE && existsSync(path.join(process.resourcesPath,'app-update.yml'))) {
  launcherUpdates=new LauncherUpdates({updater:electronUpdater.autoUpdater,
    prompt:options=>dialog.showMessageBox(window,options),
    progress:value=>{if(!window.isDestroyed())window.setProgressBar(value);},
    log:text=>liveDiagnostics?.append('launcher',text,{tag:'update'}),
    canInstall:()=>!busy && !setup?.status?.active && !aiDiagnostics?.controller,
    install:async()=>{
      installUpdateOnQuit=true;
      try {await requestClose();} finally {if(!quitting)installUpdateOnQuit=false;}
    }});
  setTimeout(()=>{if(!quitting)void launcherUpdates.check();},2000).unref();
}
if (setup) await setup.check();
if (smoke) {
  const { uiSmoke } = await import('./tests/ui-smoke.mjs');
  await uiSmoke(window, path.join(root, 'out/launcher/smoke'), publicState, uiErrors);
  const { aiChatSmoke } = await import('./tests/ai-chat-smoke.mjs');
  window.webContents.send('axrb:changed', publicState());
  await aiChatSmoke(window, aiDiagnostics, chatgpt, path.join(root, 'out/launcher/smoke'));
  const { libraryActionsSmoke } = await import('./tests/library-actions-smoke.mjs');
  await libraryActionsSmoke(window, { state, runtime, dialog, persist, publicState, ovrport });
  app.quit();
} else { if (!setup || setup.status.phase === 'ready') syncInstalled().catch(() => {}); refreshArtwork().catch(() => {}); }
app.on('window-all-closed', () => { for (const controller of controllers.values()) controller.abort(); setup?.cancel(); if (setup?.task) setup.task.finally(() => app.quit()); else app.quit(); });
}
app.whenReady().then(bootstrap).catch(error => { console.error(message(error)); if (!smoke) dialog.showErrorBox('AXRB could not start', message(error)); app.exit(1); });
