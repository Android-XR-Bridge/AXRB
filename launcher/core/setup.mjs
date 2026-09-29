import fs from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { downloadFile, checkSpace } from './download.mjs';
import { extractZip } from './archive.mjs';
import { run, powershellArgs } from './runtime.mjs';
import { redact } from './diagnostics.mjs';

export const exists = file => fs.access(file).then(() => true, () => false);
const readJson = async file => { try { return JSON.parse(await fs.readFile(file, 'utf8')); } catch { return null; } };
export const hardwareRequirementsMet = hardware => Boolean(hardware?.supportedGpu && hardware.x64 && hardware.memoryGB >= 12);
export const supportedSystem = hardware => Boolean(hardware?.hypervisor && hardwareRequirementsMet(hardware));
export function setupPhase(hardware, { ready = false, debug = false } = {}) {
  if (!debug && !hardwareRequirementsMet(hardware)) return 'unsupported';
  if (!hardware?.hypervisor) return 'hypervisor';
  return ready ? 'ready' : 'install';
}
export function officialDownload(value) {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.hostname !== 'dl.google.com' || url.port || url.username || url.password || !url.pathname.startsWith('/android/repository/')) throw new Error('Untrusted runtime download URL');
  return url;
}
export async function verify(file, component) {
  const hash = createHash(component.sha256 ? 'sha256' : 'sha1');
  try {
    if ((await fs.stat(file)).size !== component.size) return false;
    for await (const chunk of createReadStream(file)) hash.update(chunk);
    return hash.digest('hex') === (component.sha256 || component.sha1);
  } catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}
// A copied or renamed folder leaves the AVD pointing at the system image in
// its old location. Only that line is ever repointed; disk size and every
// other setting stay untouched.
function parseImageDirectory(configText) {
  const match = /^image\.sysdir\.1\s*=\s*(.+?)\s*$/im.exec(String(configText ?? ''));
  return match ? match[1].replace(/[\\/]+$/, '') : null;
}
function withImageDirectory(configText, image) {
  const text = String(configText ?? ''), line = `image.sysdir.1=${image}${path.sep}`;
  return /^image\.sysdir\.1\s*=.*$/im.test(text)
    ? text.replace(/^image\.sysdir\.1\s*=.*$/im, line)
    : `${text.replace(/\n*$/, '\n')}${line}\n`;
}
const pathKey = value => process.platform === 'win32' ? path.resolve(value).toLowerCase() : path.resolve(value);
const sameDirectory = (a, b) => Boolean(a) && Boolean(b) && pathKey(a) === pathKey(b);
const realPath = file => fs.realpath(file).catch(error => { if (error.code === 'ENOENT') return path.resolve(file); throw error; });
const containsPath = (directory, file) => {
  const relative = path.relative(directory, file);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
};
const systemImageDirectory = sdk => path.join(sdk, 'system-images/android-36/google_apis/x86_64');
function parseAvdPath(iniText) {
  const match = /^path\s*=\s*(.+?)\s*$/im.exec(String(iniText ?? ''));
  return match ? match[1].replace(/[\\/]+$/, '') : null;
}
// Match user-picked archives to known components without trusting names.
// Exact byte size selects candidates, then the pinned checksum confirms the
// match. Never mutates settings/status and never persists picked paths.
export async function identifyArchives(paths, components) {
  const list = components ?? [];
  if (!Array.isArray(paths)) throw new Error('Choose component archives as a list of files.');
  if (paths.length > list.length) throw new Error(`Choose at most ${list.length} component archives.`);
  const seenPaths = new Set(), seenIds = new Set(), identified = [];
  for (const candidate of paths) {
    if (typeof candidate !== 'string' || !path.isAbsolute(candidate)) throw new Error(`Choose an absolute archive path: ${String(candidate)}.`);
    const key = pathKey(await fs.realpath(candidate).catch(error => { if (error.code === 'ENOENT') throw new Error(`Archive not found: ${candidate}.`); throw error; }));
    if (seenPaths.has(key)) throw new Error(`The same archive was chosen twice: ${candidate}.`);
    seenPaths.add(key);
    let stat = null;
    try { stat = await fs.stat(candidate); }
    catch (error) { if (error.code === 'ENOENT') throw new Error(`Archive not found: ${candidate}.`); throw error; }
    if (!stat.isFile()) throw new Error(`Not a file: ${candidate}.`);
    const sized = list.filter(c => c.size === stat.size);
    if (!sized.length) throw new Error(`Unknown archive (size matches no component): ${candidate}.`);
    let matched = null;
    for (const component of sized) if (await verify(candidate, component)) { matched = component; break; }
    if (!matched) throw new Error(`Archive failed verification for ${sized.map(c => c.name || c.id).join(', ')}: ${candidate}.`);
    if (seenIds.has(matched.id)) throw new Error(`Two archives match the same component ${matched.name || matched.id}.`);
    seenIds.add(matched.id);
    identified.push({ id: matched.id, name: matched.name, path: candidate });
  }
  return identified;
}
export function avdConfig(image, settings) {
  return Object.entries({ 'avd.ini.encoding': 'UTF-8', 'AvdId': settings.avd, 'avd.ini.displayname': 'AXRB',
    'abi.type': 'x86_64', 'hw.cpu.arch': 'x86_64', 'hw.cpu.ncore': settings.cpuCores,
    'hw.ramSize': settings.memoryMB, 'hw.gpu.enabled': 'yes', 'hw.gpu.mode': 'host',
    'hw.audioOutput': 'yes', 'hw.audioInput': 'yes', 'hw.lcd.width': 1080, 'hw.lcd.height': 1920,
    'hw.lcd.density': 420, 'hw.keyboard': 'no', 'hw.mainKeys': 'no', 'hw.useext4': 'yes',
    'disk.dataPartition.size': `${settings.storageGB ?? 32}G`, 'disk.cachePartition.size': '66MB', 'vm.heapSize': 576,
    'image.sysdir.1': image + path.sep, 'tag.id': 'google_apis', target: 'android-36',
    'fastboot.forceColdBoot': 'no', 'fastboot.forceFastBoot': 'yes', 'showDeviceFrame': 'no',
    'runtime.network.speed': 'full', 'runtime.network.latency': 'none', 'PlayStore.enabled': 'no',
  }).map(([key, value]) => `${key}=${value}`).join('\n') + '\n';
}

export const STORAGE_MIN_GB = 8, STORAGE_MAX_GB = 256;
export function avdDirectory(settings) {
  // Managed installs keep their AVDs beside the runtime; a developer SDK uses
  // Android's default location.
  const home = process.env.ANDROID_AVD_HOME || path.join(os.homedir(), '.android', 'avd');
  return path.join(home, `${settings.avd}.avd`);
}
export function parseStorageGB(configText) {
  const match = /^disk\.dataPartition\.size\s*=\s*(\d+(?:\.\d+)?)\s*([kmgt])?/im.exec(String(configText ?? ''));
  if (!match) return null;
  const scale = { k: 1 / 1024 ** 2, m: 1 / 1024, g: 1, t: 1024 }[(match[2] || 'g').toLowerCase()];
  return Number(match[1]) * scale;
}
export function withStorageGB(configText, storageGB) {
  const text = String(configText ?? ''), line = `disk.dataPartition.size=${storageGB}G`;
  return /^disk\.dataPartition\.size\s*=.*$/im.test(text)
    ? text.replace(/^disk\.dataPartition\.size\s*=.*$/im, line)
    : `${text.replace(/\n*$/, '\n')}${line}\n`;
}
// The emulator grows a data partition on its own (it ships resize2fs), but
// nothing shrinks one: ext4 cannot give back space the guest already holds, so
// a smaller disk would mean erasing Android and every installed game.
export function planStorageChange(currentGB, requestedGB) {
  if (!Number.isInteger(requestedGB) || requestedGB < STORAGE_MIN_GB || requestedGB > STORAGE_MAX_GB) {
    throw new Error(`Choose ${STORAGE_MIN_GB}–${STORAGE_MAX_GB} GB of Android storage.`);
  }
  if (currentGB !== null && requestedGB < currentGB) {
    throw new Error(`Android storage is ${currentGB} GB and can only grow. Making it smaller would erase Android and your installed games.`);
  }
  return currentGB === null || requestedGB > currentGB;
}
const managedAvd = 'axrb-managed-api36';
async function installationAt(directory) {
  const avd = path.join(directory, 'avd', `${managedAvd}.avd`);
  if (!await exists(path.join(avd, 'userdata-qemu.img'))) return null;
  let config;
  try { config = await fs.readFile(path.join(avd, 'config.ini'), 'utf8'); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  return { directory, storageGB: parseStorageGB(config) };
}

export class Setup {
  constructor({ root, portableRoot = '', directory, currentDirectory = directory, resumableDirectory = '', runtime, components = [], select = async () => {}, stage = async () => {}, save, changed, debug = false, shutdownGraceMs = 180000, onOutput = () => {} }) {
    Object.assign(this, { root, portableRoot, directory, currentDirectory, resumableDirectory, runtime, components, select, stage, save, changed, debug, shutdownGraceMs, onOutput });
    this.status = { phase: 'checking', directory, portableRoot, current: null, storageGB: runtime.settings?.storageGB ?? 32, completed: 0, total: 0, active: false, startedAt: 0, logs: [], debug };
    this.logPartials = new Map();
  }
  update(value) {
    // Name the component so a transcript says what is downloading, not just that something is.
    const phaseChanged = value.phase && value.phase !== this.status.phase;
    const componentChanged = 'component' in value && value.component && value.component !== this.status.component;
    if (phaseChanged || componentChanged) {
      const component = 'component' in value ? value.component : this.status.component;
      this.onOutput(`Setup: ${value.phase || this.status.phase}${component ? ` · ${component}` : ''}\n`);
    }
    if (value.error && value.error !== this.status.error) this.onOutput(`Setup failed: ${value.error}\n`, { level: 'E' });
    Object.assign(this.status, value);
    if (Object.keys(value).every(k => ['completed', 'total'].includes(k)) && Date.now() - (this.lastProgress || 0) < 100) return;
    this.lastProgress = Date.now(); this.changed();
  }
  appendLog(text, metadata) {
    this.onOutput(String(text || ''), metadata);
    const stream = metadata?.stream;
    const lines = ((stream ? this.logPartials.get(stream) || '' : '') + String(text || '')).replaceAll('\r', '').split('\n');
    if (stream && !metadata.end) {
      const rest = lines.pop();
      if (rest.length > 2 * 1024 * 1024) { lines.push(rest); this.logPartials.delete(stream); }
      else if (rest) this.logPartials.set(stream, rest);
      else this.logPartials.delete(stream);
    } else if (stream) this.logPartials.delete(stream);
    const completed = lines.filter(Boolean).map(line => redact(line));
    if (!completed.length) return;
    const logs = [...(this.status.logs || []), ...completed].slice(-120);
    const now = Date.now();
    if (now - (this.lastLogUpdate || 0) < 150 && logs.length < 120) {
      this.status.logs = logs;
      return;
    }
    this.lastLogUpdate = now;
    this.update({ logs });
  }
  async runtimeHash() { return createHash('sha256').update(await fs.readFile(path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk'))).digest('hex'); }
  environment() {
    process.env.AXRB_DATA_HOME = path.join(this.directory, 'output');
    process.env.ANDROID_AVD_HOME = path.join(this.directory, 'avd');
    process.env.ANDROID_USER_HOME = path.join(this.directory, 'android');
    if (this.portableRoot) process.env.ANDROID_EMULATOR_HOME = process.env.ANDROID_USER_HOME;
    // Keep AXRB's emulator transport away from Android Studio, Quest tools,
    // and other emulators that may own the default ADB server on 5037.
    process.env.ANDROID_ADB_SERVER_PORT = '5038';
    // Platform-Tools 37 switched Windows USB discovery backends. Keep AXRB's
    // isolated server on the previous backend until its crash is resolved.
    process.env.ADB_USB_LEGACY = '1';
    // Settings allow console ports up to 5682, past adb's own scan ceiling.
    // Whichever process starts the shared server must widen it, or an emulator
    // on a high port is never discovered.
    process.env.ADB_LOCAL_TRANSPORT_MAX_PORT = '5683';
    delete process.env.ADB_SERVER_SOCKET;
    process.env.ANDROID_HOME = this.runtime.settings.sdk;
    process.env.ANDROID_SDK_ROOT = this.runtime.settings.sdk;
  }
  async inspect(directory = this.directory) {
    const sdk = path.join(directory, 'sdk'), avd = path.join(directory, 'avd', `${managedAvd}.avd`);
    const components = [];
    for (const c of this.components) {
      const destination = path.join(sdk, c.destination);
      const receipt = await readJson(path.join(destination, '.axrb-component.json'));
      if (!await exists(path.join(destination, c.probe)) || receipt?.digest !== (c.sha256 || c.sha1)) {
        components.push({ id: c.id, name: c.name, size: c.size, reason: 'Missing or outdated component' });
      }
    }
    const config = await fs.readFile(path.join(avd, 'config.ini'), 'utf8').catch(error => { if (error.code === 'ENOENT') return ''; throw error; });
    const ini = await fs.readFile(avd.slice(0, -4) + '.ini', 'utf8').catch(error => { if (error.code === 'ENOENT') return ''; throw error; });
    const android = await exists(path.join(avd, 'userdata-qemu.img'));
    const moved = Boolean(config) && (!sameDirectory(parseImageDirectory(config), systemImageDirectory(sdk)) || !sameDirectory(parseAvdPath(ini), avd));
    const receipt = await readJson(path.join(directory, 'ready.json'));
    const runtime = !receipt?.runtimeHash || receipt.runtimeHash !== await this.runtimeHash();
    const licensed = (await readJson(path.join(directory, 'license-acceptance.json')))?.license === 'android-sdk-license';
    return { directory, components, downloadBytes: components.reduce((sum, c) => sum + c.size, 0),
      avd: !config, moved, runtime, android, licensed, fresh: !android,
      ready: !components.length && Boolean(config) && !moved && android && !runtime && licensed };
  }
  async refreshCurrent() {
    // During a move, keep the previously active disk available while the
    // selected destination is still being prepared.
    let current = await installationAt(this.currentDirectory) || await installationAt(this.directory);
    // A failed fresh install must not hide the previous disk's reuse option.
    if (!current && this.status.current && this.status.current.directory !== this.directory) {
      current = await installationAt(this.status.current.directory);
    }
    const needs = await this.inspect();
    const currentNeeds = current && !sameDirectory(current.directory, this.directory) ? await this.inspect(current.directory) : null;
    this.update({ current, needs, currentNeeds });
  }
  async check() {
    if (this.status.active || this.starting) return;
    this.update({ phase: 'checking', error: '' });
    try {
      await this.refreshCurrent();
      const hardware = JSON.parse(await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/emulator/check_windows.ps1'), {})));
      this.update({ hardware, phase: setupPhase(hardware, { ready: this.status.needs.ready, debug: this.debug }) });
    } catch (error) { this.update({ phase: 'error', error: error.message }); }
  }
  async start({ directory, accepted, storageGB = 32, useCurrent = false, archives = [], forceReinstall = false }) {
    if (this.status.active || this.starting) throw new Error('Setup is already running.');
    this.starting = true;
    try {
    if (forceReinstall && !this.debug) throw new Error('Reinstalling the runtime from scratch is a debug-only option.');
    this.forceReinstall = forceReinstall === true;
    if (accepted !== true && !this.status.needs?.licensed && !this.status.currentNeeds?.licensed) throw new Error('Accept the Android SDK license to set up Android.');
    if (!this.debug && !hardwareRequirementsMet(this.status.hardware)) throw new Error('Resolve the system requirements first.');
    if (!this.status.hardware?.hypervisor) throw new Error('Enable the Windows hypervisor first.');
    // Own a child directory only; never replace user-selected directories themselves.
    let selected;
    if (useCurrent) {
      selected = this.status.current?.directory;
      if (!selected) throw new Error('No current Android installation was found. Choose a new installation folder.');
    } else {
      if (typeof directory !== 'string' || !path.isAbsolute(directory) || /[\r\n]/.test(directory)) throw new Error('Choose an absolute installation folder.');
      selected = path.join(directory, 'AXRB Runtime');
    }
    const existing = await installationAt(selected);
    if (useCurrent) {
      if (!existing) throw new Error(`The current Android disk is no longer available at ${selected}. Check the drive or choose a new installation folder.`);
      if (existing.storageGB === null) throw new Error(`Cannot determine the Android disk size at ${selected}. Restore its config.ini or choose a new installation folder. The disk has not been changed.`);
      storageGB = existing.storageGB;
    } else if (existing && sameDirectory(selected, this.resumableDirectory)) {
      if (existing.storageGB === null) throw new Error(`Cannot determine the Android disk size at ${selected}. Restore its config.ini before retrying setup.`);
      storageGB = existing.storageGB;
    } else if (existing) {
      const size = existing.storageGB === null ? 'size unknown' : `${existing.storageGB} GB`;
      throw new Error(`An Android installation already exists at ${selected} (${size}). Choose a different folder for a new installation, or select Use current Android installation to keep the current disk.`);
    }
    if (!Number.isInteger(storageGB) || storageGB < STORAGE_MIN_GB || storageGB > STORAGE_MAX_GB) throw new Error('Choose 8–256 GB of Android storage.');
    const needs = await this.inspect(selected);
    if (accepted !== true && (!needs.licensed || needs.components.length)) throw new Error('Accept the Android SDK license to set up Android.');
    const selectedArchives = useCurrent ? [] : await identifyArchives(archives, this.components);
    if (selectedArchives.length) {
      const selectedRoot = await realPath(selected), cacheRoot = await realPath(path.join(selected, 'downloads'));
      const replaced = await Promise.all(['sdk', 'avd', 'android', 'output', 'license-acceptance.json',
        `avd/${managedAvd}.ini`, `avd/${managedAvd}.avd/config.ini`,
        ...this.components.map(c => `downloads/${c.id}-extract`)].map(name => realPath(path.join(selected, name))));
      for (const archive of selectedArchives) {
        archive.path = await fs.realpath(archive.path);
        if ((containsPath(selectedRoot, archive.path) && !sameDirectory(path.dirname(archive.path), cacheRoot))
          || replaced.some(directory => containsPath(directory, archive.path))) {
          throw new Error(`Keep selected archives outside runtime files that setup replaces: ${archive.path}`);
        }
      }
    }
    // Remember the selected disk before setup changes it, so a completed
    // custom installation can still be found if the library write fails.
    await this.select(selected);
    this.selectedArchives = new Map(selectedArchives.map(archive => [archive.id, archive.path]));
    this.directory = selected;
    // Setup settings are provisional until the selected disk is ready.
    this.runtime.settings = { ...this.runtime.settings, sdk: path.join(this.directory, 'sdk'), avd: managedAvd, storageGB };
    this.environment();
    this.controller = new AbortController();
    this.update({ phase: 'verify', component: '', directory: this.directory, needs, storageGB, active: true, startedAt: Date.now(), cancelling: false, error: '', completed: 0, total: 0, logs: [] });
    this.task = this.install().catch(error => this.update({ phase: this.controller.signal.aborted ? 'cancelled' : 'error', error: this.controller.signal.aborted ? '' : error.message }))
      .finally(async () => {
        try { await this.refreshCurrent(); }
        catch (error) { this.update({ phase: 'error', error: error.message }); }
        this.update({ active: false }); this.controller = null;
      });
    } finally { this.starting = false; }
  }
  cancel() { if (this.controller) { this.update({ cancelling: true }); this.controller.abort(); } }
  async install() {
    const signal = this.controller.signal, sdk = this.runtime.settings.sdk;
    const cache = path.join(this.directory, 'downloads');
    await fs.mkdir(cache, { recursive: true });
    if (await this.runtime.online()) throw new Error('Close the running Android emulator before setting up or updating its files.');
    const dataImage = path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd/userdata-qemu.img`);
    if (!await exists(dataImage)) {
      const remaining = (await Promise.all(this.components.map(async c => await exists(path.join(sdk, c.destination, c.probe)) ? 0 : c.id === 'image' ? 6.1 : c.id === 'emulator' ? 1.6 : 0.3))).reduce((a, b) => a + b, 0);
      const space = await fs.statfs(this.directory), availableGB = Number(space.bavail) * Number(space.bsize) / 1024 ** 3;
      const requiredGB = this.runtime.settings.storageGB * 1.2 + remaining + 5;
      if (availableGB < requiredGB) throw new Error(`Setup needs ${Math.ceil(requiredGB)} GB free here (${availableGB.toFixed(1)} GB available). Choose another drive or a smaller Android disk.`);
    }
    // A failed repair must not inherit a previous installation's success.
    await fs.rm(path.join(this.directory, 'ready.json'), { force: true });
    await fs.writeFile(path.join(this.directory, 'license-acceptance.json'), JSON.stringify({ license: 'android-sdk-license', acceptedAt: new Date().toISOString() }));
    for (const c of this.components) {
      signal.throwIfAborted();
      if (!c.sha256 && !c.sha1) throw new Error(`${c.name}: missing download checksum.`);
      const destination = path.join(sdk, c.destination), receipt = path.join(destination, '.axrb-component.json');
      if (await exists(path.join(destination, c.probe)) && (await readJson(receipt))?.digest === (c.sha256 || c.sha1)) continue;
      await checkSpace(this.directory, c.id === 'image' ? 9 * 1024 ** 3 : c.size * 4);
      const selected = this.selectedArchives?.get(c.id), ownedBySetup = !selected;
      let archive = selected || path.join(cache, `${c.id}.zip`);
      if (ownedBySetup && this.selectedArchives?.size) {
        const protectedPaths = new Set([...(this.selectedArchives?.values() || [])].map(pathKey));
        let suffix = 0;
        while ((await Promise.all([archive, archive + '.part', archive + '.part.json'].map(async file => protectedPaths.has(pathKey(await realPath(file)))))).some(Boolean)) {
          archive = path.join(cache, `${c.id}-download-${++suffix}.zip`);
        }
      }
      this.update({ phase: 'verify', component: c.name, completed: 0, total: c.size });
      const verified = await verify(archive, c);
      if (!verified && !ownedBySetup) throw new Error(`Selected ${c.name} archive changed or disappeared: ${archive}`);
      if (!verified) {
        await fs.rm(archive, { force: true });
        this.update({ phase: 'download' });
        await downloadFile({ ...c, destination: archive, signal, validate: officialDownload, progress: (completed, total) => this.update({ completed, total }) });
      }
      const staging = path.join(cache, `${c.id}-extract`);
      await fs.rm(staging, { recursive: true, force: true });
      this.update({ phase: 'extract', completed: 0, total: 0 });
      try {
        await extractZip(archive, staging, { signal, progress: (completed, total) => this.update({ completed, total }) });
        const source = path.join(staging, c.prefix);
        if (!await exists(path.join(source, c.probe))) throw new Error(`${c.name}: expected files are missing.`);
        if (c.id === 'emulator') {
          const qemu = path.join(source, 'qemu/windows-x86_64/qemu-system-x86_64-headless.exe');
          const hash = createHash('sha256'); for await (const bytes of createReadStream(qemu)) hash.update(bytes);
          if (hash.digest('hex') !== 'dcec1cc23ac57ff04ec748cde7e42bfc713bf2ad532e49606a4a9332cfb94b56') throw new Error('Emulator is incompatible with the AXRB clock adapter.');
        }
        await fs.mkdir(path.dirname(destination), { recursive: true });
        await fs.rm(destination, { recursive: true, force: true });
        await fs.rename(source, destination);
        await fs.writeFile(receipt, JSON.stringify({ digest: c.sha256 || c.sha1 }));
      } finally { await fs.rm(staging, { recursive: true, force: true }); }
      if (ownedBySetup) await fs.rm(archive, { force: true });
    }
    signal.throwIfAborted();
    const avd = path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd`);
    await fs.mkdir(avd, { recursive: true });
    const configFile = path.join(avd, 'config.ini'), image = systemImageDirectory(sdk);
    if (!await exists(dataImage) || !await exists(configFile)) await fs.writeFile(configFile, avdConfig(image, this.runtime.settings));
    else {
      const config = await fs.readFile(configFile, 'utf8');
      const ini = await fs.readFile(avd.slice(0, -4) + '.ini', 'utf8').catch(error => { if (error.code === 'ENOENT') return ''; throw error; });
      if (!sameDirectory(parseImageDirectory(config), image) || !sameDirectory(parseAvdPath(ini), avd)) {
        await fs.writeFile(configFile, withImageDirectory(config, image));
        await fs.rm(path.join(avd, 'snapshots/default_boot'), { recursive: true, force: true });
      }
    }
    await fs.writeFile(avd.slice(0, -4) + '.ini', `avd.ini.encoding=UTF-8\npath=${avd}\ntarget=android-36\n`);
    await fs.mkdir(process.env.ANDROID_USER_HOME, { recursive: true });
    // A user-selected port can belong to another AVD. Never modify or stop it.
    if (await this.runtime.online()) throw new Error('The setup Android port is in use. Close that emulator and retry.');
    this.update({ phase: 'boot', component: 'Starting Android', completed: 0, total: 0, logs: [] });
    let installed;
    try {
      await this.runtime.ensure({ onOutput: (text, metadata) => this.appendLog(text, metadata) });
      this.appendLog('Android boot completed; verifying GPU and ABI.\n');
      signal.throwIfAborted();
      if (this.forceReinstall) {
        this.update({ component: 'Uninstalling AXRB runtime (debug)' });
        this.appendLog('Debug: uninstalling the existing AXRB runtime before installing, instead of updating in place.\n');
        // A mismatched result here is exactly what forces this path in the first
        // place (Android refuses to update an app across a different signing
        // key), so a missing package or any other uninstall failure is fine to
        // ignore: the install below is the real, observable outcome.
        await this.runtime.adb(['uninstall', 'com.axrb.openxrruntime']).catch(() => {});
      }
      this.update({ component: 'Installing AXRB runtime' });
      await this.runtime.adb(['install', '--no-incremental', '--force-queryable', '-r', path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk')], { timeout: 240000 });
      if (!(await this.runtime.adb(['shell', 'pm', 'path', 'com.axrb.openxrruntime'])).includes('package:')) throw new Error('Android did not register the AXRB runtime. Retry setup.');
      installed = await this.runtime.installed();
      if (!installed) throw new Error('Android disconnected before its installed games could be checked. Retry setup.');
      await this.runtime.adb(['shell', 'sync']);
      this.appendLog('AXRB runtime installed and synchronized.\n');
      signal.throwIfAborted();
    } finally {
      const name = await this.runtime.adb(['emu', 'avd', 'name']).catch(() => '');
      if (name.split(/\r?\n/)[0].trim() === this.runtime.settings.avd) {
        this.update({ component: 'Shutting down Android' });
        await this.runtime.adb(['emu', 'kill']).catch(() => {});
        const lock = path.join(avd, 'hardware-qemu.ini.lock');
        // Shutdown writes a quick-boot snapshot, so it scales with guest RAM
        // and disk speed and can run for minutes. Everything setup installs is
        // already on disk by now, so a slow shutdown is reported, never fatal:
        // failing here discarded a complete install and forced the whole run
        // again. A lock left behind is cleaned up by the next emulator start.
        const deadline = Date.now() + this.shutdownGraceMs;
        while (Date.now() < deadline && await exists(lock)) await new Promise(resolve => setTimeout(resolve, 500));
        if (await exists(lock)) this.appendLog('Android is still shutting down in the background; setup is complete and you can start it now.\n');
      }
    }
    signal.throwIfAborted();
    await this.stage(this.directory, installed);
    await fs.writeFile(path.join(this.directory, 'ready.json'), JSON.stringify({ version: 1, runtimeHash: await this.runtimeHash(), completedAt: new Date().toISOString() }));
    await this.save(this.directory, installed);
    this.currentDirectory = this.directory;
    this.resumableDirectory = '';
    this.update({ phase: 'ready', component: '', completed: 0, total: 0 });
  }
}
