import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { archivePath, extractZip } from '../core/archive.mjs';
import { Setup, verify, officialDownload, avdConfig, hardwareRequirementsMet, setupPhase, supportedSystem } from '../core/setup.mjs';
import { State } from '../core/state.mjs';
import { run } from '../core/runtime.mjs';

function restoreSetupEnvironment(t) {
  const old = { ...process.env };
  t.after(() => {
    for (const name of ['AXRB_DATA_HOME', 'ANDROID_AVD_HOME', 'ANDROID_USER_HOME', 'ANDROID_HOME', 'ANDROID_SDK_ROOT',
      'ANDROID_ADB_SERVER_PORT', 'ADB_LOCAL_TRANSPORT_MAX_PORT', 'ADB_SERVER_SOCKET']) {
      if (old[name] === undefined) delete process.env[name]; else process.env[name] = old[name];
    }
  });
}

test('runtime download trust excludes local URLs, credentials and unexpected hosts', () => {
  assert.equal(officialDownload('https://dl.google.com/android/repository/a.zip').hostname, 'dl.google.com');
  for (const value of ['http://dl.google.com/android/repository/a.zip', 'https://dl.google.com.evil.test/android/repository/a.zip', 'https://user@dl.google.com/android/repository/a.zip', 'https://127.0.0.1/a', 'https://dl.google.com/other.zip']) assert.throws(() => officialDownload(value));
});
test('archive paths cannot escape extraction or address alternate streams', () => {
  const root = path.resolve('scratch');
  for (const name of ['../outside', '/absolute', 'C:/file', 'dir\\file', 'file:stream', 'dir./file', 'dir /file']) assert.throws(() => archivePath(root, name));
  assert.equal(archivePath(root, 'emulator/emulator.exe'), path.join(root, 'emulator/emulator.exe'));
});
test('component verification rejects truncated and corrupted files', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-verify-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const file = path.join(dir, 'component'), data = Buffer.from('official content');
  const component = { size: data.length, sha256: createHash('sha256').update(data).digest('hex') };
  assert.equal(await verify(file, component), false);
  await fs.writeFile(file, data); assert.equal(await verify(file, component), true);
  await fs.writeFile(file, Buffer.alloc(data.length)); assert.equal(await verify(file, component), false);
  await fs.writeFile(file, data.subarray(1)); assert.equal(await verify(file, component), false);
});
test('AVD uses hardware graphics and an independently located Android image', () => {
  const config = avdConfig('D:\\Games\\Android', { avd: 'test', cpuCores: 4, memoryMB: 8192 });
  assert.match(config, /hw.gpu.mode=host/); assert.match(config, /hw.cpu.ncore=4/);
  assert.match(config, /disk.dataPartition.size=32G/); assert.match(config, /image.sysdir.1=D:\\Games\\Android/);
});
test('setup refuses missing license or virtualization before touching runtime', async () => {
  const setup = new Setup({ directory: 'unused', runtime: {}, changed() {} });
  await assert.rejects(setup.start({ directory: 'unused', accepted: false }), /license/);
  await assert.rejects(setup.start({ directory: 'unused', accepted: true }), /requirements/);
});
test('failed boot is retryable and never produces a ready receipt', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-setup-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  restoreSetupEnvironment(t);
  let killed = false;
  const avd = path.join(dir, 'AXRB Runtime/avd/axrb-managed-api36.avd');
  await fs.mkdir(avd, { recursive: true }); await fs.writeFile(path.join(avd, 'userdata-qemu.img'), 'fixture');
  await fs.writeFile(path.join(avd, 'config.ini'), 'disk.dataPartition.size=32G\n');
  const runtime = { settings: { cpuCores: 4, memoryMB: 8192 }, online: async () => false, ensure: async () => { throw new Error('boot failed'); }, adb: async args => { if (args[1] === 'kill') killed = true; return 'unrelated-avd\nOK'; } };
  const setup = new Setup({ root: dir, directory: dir, runtime, components: [], save: async () => {}, changed() {} });
  setup.status.hardware = { hypervisor: true, supportedGpu: true, x64: true, memoryGB: 16 };
  setup.status.current = { directory: path.dirname(path.dirname(avd)), storageGB: 32 };
  await setup.start({ useCurrent: true, accepted: true }); await setup.task;
  assert.equal(setup.status.phase, 'error'); assert.match(setup.status.error, /boot failed/); assert.equal(killed, false);
  await assert.rejects(fs.access(path.join(setup.directory, 'ready.json')));
  assert.equal(setup.status.active, false);
});

test('system eligibility accepts AMD without requiring NVIDIA and rejects unsupported systems', () => {
  const amd = { hypervisor: true, supportedGpu: true, x64: true, memoryGB: 16, gpu: 'AMD Radeon' };
  assert.equal(supportedSystem(amd), true);
  assert.equal(supportedSystem({ ...amd, gpu: 'NVIDIA GeForce' }), true);
  for (const patch of [{ supportedGpu: false }, { hypervisor: false }, { x64: false }, { memoryGB: 8 }]) assert.equal(supportedSystem({ ...amd, ...patch }), false);
});

test('setup shows hardware requirements before Hypervisor Platform', () => {
  const weak = { hypervisor: false, supportedGpu: false, x64: true, memoryGB: 8 };
  assert.equal(hardwareRequirementsMet(weak), false);
  assert.equal(setupPhase(weak), 'unsupported');
  assert.equal(setupPhase({ ...weak, supportedGpu: true, memoryGB: 16 }), 'hypervisor');
  assert.equal(setupPhase({ ...weak, hypervisor: true }), 'unsupported');
});

test('debug setup flag skips hardware requirements but keeps the hypervisor gate', async () => {
  const weak = { hypervisor: false, supportedGpu: false, x64: false, memoryGB: 2 };
  assert.equal(setupPhase(weak, { debug: true }), 'hypervisor');
  assert.equal(setupPhase({ ...weak, hypervisor: true }, { debug: true }), 'install');
  const setup = new Setup({ directory: 'unused', runtime: {}, debug: true, changed() {} });
  setup.status.hardware = weak;
  await assert.rejects(setup.start({ directory: 'unused', accepted: true }), /hypervisor/);
});

test('a slow Android shutdown does not discard a completed install', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-shutdown-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  restoreSetupEnvironment(t);
  await fs.mkdir(path.join(dir, 'out/android/runtime-arm64-v8a'), { recursive: true });
  await fs.writeFile(path.join(dir, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk'), 'apk');
  const avd = path.join(dir, 'AXRB Runtime/avd/axrb-managed-api36.avd');
  await fs.mkdir(avd, { recursive: true });
  // An existing data image means setup skips the free-space gate, as on a reinstall.
  await fs.writeFile(path.join(avd, 'userdata-qemu.img'), 'fixture');
  await fs.writeFile(path.join(avd, 'config.ini'), 'disk.dataPartition.size=32G\n');
  // Quick-boot snapshot save outlives the grace period: the lock never clears.
  await fs.mkdir(path.join(avd, 'hardware-qemu.ini.lock'), { recursive: true });
  const runtime = {
    settings: { cpuCores: 4, memoryMB: 8192, avd: 'axrb-managed-api36' },
    online: async () => false,
    ensure: async () => {},
    installed: async () => new Set(['com.axrb.openxrruntime']),
    adb: async args => {
      if (args[0] === 'emu' && args[1] === 'avd') return 'axrb-managed-api36\nOK';
      if (args.includes('path')) return 'package:/data/app/base.apk';
      return '';
    },
  };
  const setup = new Setup({ root: dir, directory: dir, runtime, components: [], save: async () => {}, changed() {}, shutdownGraceMs: 400 });
  setup.status.hardware = { hypervisor: true, supportedGpu: true, x64: true, memoryGB: 16 };
  setup.status.current = { directory: path.dirname(path.dirname(avd)), storageGB: 32 };
  await setup.start({ useCurrent: true, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', `setup must complete, got: ${setup.status.error}`);
  await fs.access(path.join(setup.directory, 'ready.json'));
  assert.ok(setup.status.logs.some(l => /still shutting down/i.test(l)), 'the slow shutdown is reported, not hidden');
  assert.ok(await fs.access(path.join(avd, 'hardware-qemu.ini.lock')).then(() => true, () => false), 'the lock is left for the emulator to reclaim');
});

async function installationFixture(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-destination-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  restoreSetupEnvironment(t);
  t.mock.method(fs, 'statfs', async () => ({ bavail: 1024 ** 3, bsize: 1024 }));
  const current = path.join(root, 'previous', 'AXRB Runtime');
  const avd = path.join(current, 'avd', 'axrb-managed-api36.avd');
  const config = `AvdId=axrb-managed-api36\ndisk.dataPartition.size=64G\nimage.sysdir.1=${path.join(current, 'sdk/system-images/android-36/google_apis/x86_64')}${path.sep}\n`;
  await fs.mkdir(avd, { recursive: true });
  await fs.writeFile(path.join(avd, 'config.ini'), config);
  await fs.writeFile(path.join(avd, 'userdata-qemu.img'), 'saved Android data');
  await fs.writeFile(path.join(current, 'ready.json'), '{"runtimeHash":"previous-runtime"}');
  await fs.mkdir(path.join(root, 'out/android/runtime-arm64-v8a'), { recursive: true });
  await fs.writeFile(path.join(root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk'), 'apk');
  await fs.mkdir(path.join(root, 'scripts/emulator'), { recursive: true });
  await fs.writeFile(path.join(root, 'scripts/emulator/check_windows.ps1'),
    `Write-Output '{"hypervisor":true,"supportedGpu":true,"x64":true,"memoryGB":16}'`);
  const runtime = {
    settings: { managedDirectory: current, avd: 'axrb-managed-api36', sdk: path.join(current, 'sdk'), storageGB: 32, cpuCores: 4, memoryMB: 8192 },
    online: async () => false,
    installed: async () => new Set(['com.axrb.openxrruntime', ...(runtime.settings.sdk === path.join(current, 'sdk') ? ['com.game.old', 'com.game.available'] : [])]),
    ensure: async () => {
      const image = path.join(process.env.ANDROID_AVD_HOME, 'axrb-managed-api36.avd/userdata-qemu.img');
      await fs.writeFile(image, 'new Android data', { flag: 'wx' }).catch(error => { if (error.code !== 'EEXIST') throw error; });
    },
    adb: async args => args.includes('path') ? 'package:/data/app/base.apk' : 'unrelated-avd\nOK',
  };
  const state = new State(path.join(root, 'profile')); await state.load();
  state.data.settings = runtime.settings;
  state.data.games = [
    { id: 'old', name: 'Saved game', package: 'com.game.old', installed: true, downloaded: true, owned: true, apk: path.join(root, 'downloads/game.apk') },
    { id: 'available', name: 'Available game', package: 'com.game.available', installed: false, owned: true },
    { id: 'removed', name: 'Removed game', package: 'com.game.removed', installed: true, downloaded: true },
  ];
  await state.save();
  const setup = new Setup({ root, directory: current, runtime, components: [],
    save: async (value, installed) => { state.activateRuntime(value, runtime.settings, installed); await state.save(); }, changed() {} });
  await setup.check();
  assert.equal(setup.status.phase, 'install', setup.status.error);
  return { root, current, avd, config, setup, state };
}

test('a fresh destination is independent of the saved disk and completion receipt', async t => {
  const { root, current, avd, config, setup } = await installationFixture(t);
  assert.deepEqual(setup.status.current, { directory: current, storageGB: 64 });
  const destination = path.join(root, 'brand-new');
  await setup.start({ directory: destination, storageGB: 8, accepted: true, useCurrent: false });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  const selected = path.join(destination, 'AXRB Runtime');
  assert.match(await fs.readFile(path.join(selected, 'avd/axrb-managed-api36.avd/config.ini'), 'utf8'), /disk.dataPartition.size=8G/);
  assert.equal(JSON.parse(await fs.readFile(path.join(selected, 'ready.json'), 'utf8')).runtimeHash, createHash('sha256').update('apk').digest('hex'));
  assert.equal(await fs.readFile(path.join(avd, 'config.ini'), 'utf8'), config);
  assert.equal(await fs.readFile(path.join(avd, 'userdata-qemu.img'), 'utf8'), 'saved Android data');
  assert.equal(await fs.readFile(path.join(current, 'ready.json'), 'utf8'), '{"runtimeHash":"previous-runtime"}');
  assert.deepEqual(setup.status.current, { directory: selected, storageGB: 8 });
});

test('explicit reuse rechecks the disk size and never resizes from stale settings', async t => {
  const { root, current, avd, setup } = await installationFixture(t);
  // The disk configuration can change after discovery; neither the displayed
  // size nor the saved profile nor a submitted size may override it.
  const config = 'AvdId=axrb-managed-api36\ndisk.dataPartition.size=96G\n';
  await fs.writeFile(path.join(avd, 'config.ini'), config);
  const ignored = path.join(root, 'must-not-create');
  await setup.start({ useCurrent: true, directory: ignored, storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  assert.match(await fs.readFile(path.join(avd, 'config.ini'), 'utf8'), /disk\.dataPartition\.size=96G/);
  assert.equal(await fs.readFile(path.join(avd, 'userdata-qemu.img'), 'utf8'), 'saved Android data');
  assert.equal(setup.status.storageGB, 96);
  assert.deepEqual(setup.status.current, { directory: current, storageGB: 96 });
  await assert.rejects(fs.access(ignored), { code: 'ENOENT' });
});

test('new mode refuses an occupied selected folder before modifying either disk', async t => {
  const { root, current, avd, config, setup } = await installationFixture(t);
  const destination = path.join(root, 'occupied');
  const target = path.join(destination, 'AXRB Runtime/avd/axrb-managed-api36.avd');
  await fs.mkdir(target, { recursive: true });
  await fs.writeFile(path.join(target, 'userdata-qemu.img'), 'other Android data');
  await fs.writeFile(path.join(target, 'config.ini'), 'disk.dataPartition.size=8G\n');
  await assert.rejects(setup.start({ directory: destination, storageGB: 8, accepted: true, useCurrent: false }),
    error => error.message.includes(path.join(destination, 'AXRB Runtime')) && error.message.includes('8 GB'));
  assert.equal(await fs.readFile(path.join(target, 'userdata-qemu.img'), 'utf8'), 'other Android data');
  assert.equal(await fs.readFile(path.join(target, 'config.ini'), 'utf8'), 'disk.dataPartition.size=8G\n');
  await assert.rejects(fs.access(path.join(destination, 'AXRB Runtime/downloads')), { code: 'ENOENT' });
  assert.equal(await fs.readFile(path.join(avd, 'config.ini'), 'utf8'), config);
  assert.equal(await fs.readFile(path.join(current, 'ready.json'), 'utf8'), '{"runtimeHash":"previous-runtime"}');
});

test('reuse refuses a missing disk or unknown disk size without recreating it', async t => {
  const { current, avd, setup } = await installationFixture(t);
  await fs.rm(path.join(avd, 'config.ini'));
  await setup.check();
  assert.deepEqual(setup.status.current, { directory: current, storageGB: null });
  await assert.rejects(setup.start({ useCurrent: true, accepted: true }), /disk size/);
  await assert.rejects(fs.access(path.join(avd, 'config.ini')), { code: 'ENOENT' });
  assert.equal(await fs.readFile(path.join(avd, 'userdata-qemu.img'), 'utf8'), 'saved Android data');
  await fs.writeFile(path.join(avd, 'config.ini'), 'hw.ramSize=8192\n');
  await assert.rejects(setup.start({ useCurrent: true, accepted: true }), /disk size/);
  assert.equal(await fs.readFile(path.join(avd, 'config.ini'), 'utf8'), 'hw.ramSize=8192\n');
  await fs.rm(path.join(avd, 'userdata-qemu.img'));
  await assert.rejects(setup.start({ useCurrent: true, accepted: true }), /no longer available/);
  await assert.rejects(fs.access(path.join(current, 'downloads')), { code: 'ENOENT' });
  await setup.check();
  assert.equal(setup.status.current, null);
});

test('a failed fresh install keeps the old disk available for reuse and retry', async t => {
  const { root, current, avd, config, setup } = await installationFixture(t);
  t.mock.method(fs, 'statfs', async () => ({ bavail: 0, bsize: 1024 }));
  await setup.start({ directory: path.join(root, 'no-space'), storageGB: 8, accepted: true, useCurrent: false });
  await setup.task;
  assert.equal(setup.status.phase, 'error');
  assert.match(setup.status.error, /GB free/);
  assert.deepEqual(setup.status.current, { directory: current, storageGB: 64 });
  await setup.check();
  assert.deepEqual(setup.status.current, { directory: current, storageGB: 64 });
  await setup.start({ useCurrent: true, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  assert.equal(await fs.readFile(path.join(avd, 'config.ini'), 'utf8'), config);
  assert.equal(await fs.readFile(path.join(avd, 'userdata-qemu.img'), 'utf8'), 'saved Android data');
});

test('failed provisioning preserves the active runtime and library across restart', async t => {
  const { root, current, setup, state } = await installationFixture(t);
  const before = structuredClone(state.data);
  setup.runtime.ensure = async () => {
    // Other launcher operations may save the profile while setup is running.
    await state.save();
    throw new Error('boot failed after provisioning');
  };
  const destination = path.join(root, 'failed-new-runtime');
  await setup.start({ directory: destination, storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'error');
  assert.match(setup.status.error, /boot failed after provisioning/);
  await fs.access(path.join(destination, 'AXRB Runtime/license-acceptance.json'));
  const reopened = new State(state.directory); await reopened.load();
  assert.deepEqual(reopened.data.settings, before.settings);
  assert.deepEqual(reopened.data.games, before.games);
  setup.runtime.settings = reopened.data.settings;
  const restarted = new Setup({ root, directory: reopened.data.settings.managedDirectory, runtime: setup.runtime,
    components: [], save: async () => {}, changed() {} });
  restarted.environment(); await restarted.check();
  assert.deepEqual(restarted.status.current, { directory: current, storageGB: 64 });
});

test('cancelling a fresh install does not replace the saved runtime', async t => {
  const { root, setup, state } = await installationFixture(t);
  const before = structuredClone(state.data);
  setup.runtime.ensure = async () => { await state.save(); setup.cancel(); };
  await setup.start({ directory: path.join(root, 'cancelled-runtime'), storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'cancelled');
  const reopened = new State(state.directory); await reopened.load();
  assert.deepEqual(reopened.data.settings, before.settings);
  assert.deepEqual(reopened.data.games, before.games);
});

test('activating a fresh disk refreshes installed flags without discarding the library', async t => {
  const { root, setup, state } = await installationFixture(t);
  const games = structuredClone(state.data.games);
  const destination = path.join(root, 'new-library-runtime');
  await setup.start({ directory: destination, storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  const reopened = new State(state.directory); await reopened.load();
  assert.equal(reopened.data.settings.managedDirectory, path.join(destination, 'AXRB Runtime'));
  assert.equal(reopened.data.settings.sdk, path.join(destination, 'AXRB Runtime/sdk'));
  assert.equal(reopened.data.settings.storageGB, 8);
  assert.deepEqual(reopened.data.games, games.map(game => ({ ...game, installed: false })));
});

test('a failed library write leaves a completed custom disk recoverable after restart', async t => {
  const { root, current, setup, state } = await installationFixture(t);
  setup.select = value => state.stageRuntime(value);
  setup.stage = (value, installed) => state.stageRuntime(value, installed);
  setup.save = (value, installed) => state.commitRuntime(value, setup.runtime.settings, installed);
  state.save = async () => { throw new Error('profile write failed'); };
  const destination = path.join(root, 'completed-custom');
  await setup.start({ directory: destination, storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'error');
  assert.match(setup.status.error, /profile write failed/);
  const reopened = new State(state.directory); await reopened.load();
  assert.equal(reopened.data.settings.managedDirectory, current);
  const pending = await reopened.pendingRuntime();
  assert.equal(pending.directory, path.join(destination, 'AXRB Runtime'));
  assert.ok(Array.isArray(pending.installed));
  const restarted = new Setup({ root, directory: pending.directory, currentDirectory: current,
    resumableDirectory: pending.directory, runtime: setup.runtime, components: [], changed() {} });
  assert.equal((await restarted.inspect()).ready, true);
  await restarted.check();
  assert.deepEqual(restarted.status.current, { directory: current, storageGB: 64 });
  await reopened.commitRuntime(pending.directory, setup.runtime.settings, new Set(pending.installed));
  const recovered = new State(state.directory); await recovered.load();
  assert.equal(recovered.data.settings.managedDirectory, pending.directory);
  assert.ok(recovered.data.games.every(game => !game.installed));
  assert.equal(await recovered.pendingRuntime(), null);
});

test('reusing a disk reconciles the library with the apps actually on that disk', async t => {
  const { current, setup, state } = await installationFixture(t);
  const games = structuredClone(state.data.games);
  await setup.start({ useCurrent: true, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  const reopened = new State(state.directory); await reopened.load();
  assert.equal(reopened.data.settings.managedDirectory, current);
  assert.deepEqual(reopened.data.games, games.map(game => ({ ...game, installed: game.id !== 'removed' })));
});

test('an unavailable installed-app inventory never commits an empty library state', async t => {
  const { root, setup, state } = await installationFixture(t);
  const before = structuredClone(state.data);
  setup.runtime.installed = async () => null;
  await setup.start({ directory: path.join(root, 'disconnected-runtime'), storageGB: 8, accepted: true });
  await setup.task;
  assert.equal(setup.status.phase, 'error');
  assert.match(setup.status.error, /disconnected/);
  const reopened = new State(state.directory); await reopened.load();
  assert.deepEqual(reopened.data.settings, before.settings);
  assert.deepEqual(reopened.data.games, before.games);
  await assert.rejects(fs.access(path.join(setup.directory, 'ready.json')), { code: 'ENOENT' });
});

test('failed moved-runtime repair remains retryable without resetting Android', async t => {
  const { root, current, avd, setup } = await installationFixture(t);
  await fs.writeFile(path.join(current, 'ready.json'), JSON.stringify({ runtimeHash: await setup.runtimeHash() }));
  await fs.writeFile(path.join(current, 'license-acceptance.json'), '{"license":"android-sdk-license"}');
  await fs.writeFile(avd.slice(0, -4) + '.ini', `path=${path.join(root, 'old-location/avd/axrb-managed-api36.avd')}\n`);
  await fs.mkdir(path.join(avd, 'snapshots/default_boot'), { recursive: true });
  await fs.writeFile(path.join(avd, 'snapshots/default_boot/memory.bin'), 'old snapshot');
  await setup.check();
  assert.equal(setup.status.needs.moved, true);
  setup.runtime.ensure = async () => { throw new Error('repair boot failed'); };
  await setup.start({ useCurrent: true, accepted: false }); await setup.task;
  assert.equal(setup.status.phase, 'error');
  assert.equal(await fs.readFile(path.join(avd, 'userdata-qemu.img'), 'utf8'), 'saved Android data');
  await assert.rejects(fs.access(path.join(avd, 'snapshots/default_boot')), { code: 'ENOENT' });
  await setup.check();
  assert.equal(setup.status.phase, 'install', 'Failed repair must not reuse the old success receipt.');
});

async function componentArchive(root, id) {
  const file = path.join(root, `renamed-${id}.zip`);
  await run('python', ['-c', 'import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1],"w") as z: z.writestr("payload/tool.exe",sys.argv[2])', file, id]);
  const bytes = await fs.readFile(file);
  return { file, bytes, component: { id, name: id, destination: id, prefix: 'payload', probe: 'tool.exe',
    size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex'),
    url: `https://dl.google.com/android/repository/fixture-${id}.zip` } };
}

test('selected archives install offline without changing their originals', async t => {
  const { root, setup } = await installationFixture(t);
  const a = await componentArchive(root, 'tools-a'), b = await componentArchive(root, 'tools-b');
  setup.components = [a.component, b.component];
  t.mock.method(globalThis, 'fetch', async () => { throw new Error('No network is allowed for a complete local selection.'); });
  await setup.start({ directory: path.join(root, 'offline'), storageGB: 8, accepted: true, archives: [a.file, b.file] });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  for (const source of [a, b]) {
    assert.equal(await fs.readFile(path.join(setup.runtime.settings.sdk, source.component.destination, 'tool.exe'), 'utf8'), source.component.id);
    assert.deepEqual(await fs.readFile(source.file), source.bytes);
  }
});

test('partial selection preserves sources through a cache junction and removes only its downloaded archive', async t => {
  const { root, setup } = await installationFixture(t);
  const a = await componentArchive(root, 'tools-a'), b = await componentArchive(root, 'tools-b');
  setup.components = [a.component, b.component];
  const destination = path.join(root, 'partial'), cache = path.join(destination, 'AXRB Runtime/downloads');
  const external = path.join(root, 'external-archives');
  await fs.mkdir(path.dirname(cache), { recursive: true }); await fs.mkdir(external);
  await fs.symlink(external, cache, 'junction');
  // An arbitrary filename can coincide with another component's cache slot.
  const selected = path.join(external, 'tools-b.zip');
  await fs.rename(a.file, selected);
  const requests = [];
  t.mock.method(globalThis, 'fetch', async url => {
    requests.push(String(url));
    assert.equal(String(url), b.component.url);
    return new Response(b.bytes, { headers: { 'content-length': String(b.bytes.length) } });
  });
  await setup.start({ directory: destination, storageGB: 8, accepted: true, archives: [selected] });
  await setup.task;
  assert.equal(setup.status.phase, 'ready', setup.status.error);
  assert.deepEqual(requests, [b.component.url]);
  assert.deepEqual(await fs.readFile(selected), a.bytes);
  assert.deepEqual(await fs.readdir(external), ['tools-b.zip']);
  assert.equal(await fs.readFile(path.join(setup.runtime.settings.sdk, 'tools-b/tool.exe'), 'utf8'), 'tools-b');
});

test('invalid explicit selections fail before provisioning instead of falling back to downloads', async t => {
  const { root, setup } = await installationFixture(t);
  const a = await componentArchive(root, 'tools-a'), b = await componentArchive(root, 'tools-b');
  setup.components = [a.component, b.component];
  const duplicate = path.join(root, 'duplicate.zip'), corrupt = path.join(root, 'corrupt.zip'), unknown = path.join(root, 'unknown.zip');
  await fs.copyFile(a.file, duplicate);
  const damaged = Buffer.from(a.bytes); damaged[0] ^= 1;
  await fs.writeFile(corrupt, damaged); await fs.writeFile(unknown, 'unknown');
  const destination = path.join(root, 'rejected');
  for (const [archives, error] of [[[corrupt], /verification/], [[unknown], /Unknown archive/], [[a.file, duplicate], /same component/]]) {
    await assert.rejects(setup.start({ directory: destination, storageGB: 8, accepted: true, archives }), error);
    await assert.rejects(fs.access(destination), { code: 'ENOENT' });
  }
  const externalSdk = path.join(root, 'external-sdk'), target = path.join(destination, 'AXRB Runtime');
  await fs.mkdir(externalSdk); await fs.mkdir(target, { recursive: true });
  const insideSdk = path.join(externalSdk, 'source.zip');
  await fs.copyFile(a.file, insideSdk);
  await fs.symlink(externalSdk, path.join(target, 'sdk'), 'junction');
  await assert.rejects(setup.start({ directory: destination, storageGB: 8, accepted: true, archives: [insideSdk] }), /outside runtime files/);
  assert.deepEqual(await fs.readFile(insideSdk), a.bytes);
  await assert.rejects(fs.access(path.join(target, 'license-acceptance.json')), { code: 'ENOENT' });
});
