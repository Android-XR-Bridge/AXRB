import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { buildProfile, checkBackendRequest } from '../core/build-profile.mjs';
import { Runtime } from '../core/runtime.mjs';
import { DirectSetup } from '../core/direct-setup.mjs';
import { LiveDiagnostics } from '../core/live-diagnostics.mjs';
import { performanceScanArgs } from '../core/performance.mjs';

test('Emulatorless identity and IPC capability cannot be enabled by settings', async () => {
  assert.equal(buildProfile.productName, 'AXRB-Emulatorless');
  assert.equal(typeof buildProfile.emulatorEnabled, 'boolean');
  assert.equal(buildProfile.updatesEnabled, false);
  const pkg = JSON.parse(await fs.readFile(new URL('../package.json', import.meta.url)));
  assert.equal(pkg.version, '2.0.0');
  assert.equal(pkg.build.appId, 'org.axrb.emulatorless');
  assert.equal(pkg.build.publish, null);
  assert.equal(pkg.build.nsis.oneClick, false);
  assert.equal(pkg.build.nsis.allowToChangeInstallationDirectory, true);
  for (const name of ['install', 'uninstall', 'startAndroid', 'stopAndroid', 'setupResume', 'chooseSetupArchives', 'storage', 'permissions', 'setPermission']) {
    assert.throws(() => checkBackendRequest(name, [], false), /disabled/);
    assert.doesNotThrow(() => checkBackendRequest(name, [], true));
  }
  assert.throws(() => checkBackendRequest('play', ['id', 'android'], false), /disabled/);
  assert.doesNotThrow(() => checkBackendRequest('play', ['id', 'direct'], false));
});

test('disabled Runtime never probes, starts or stops an emulator', async () => {
  const runtime = new Runtime('missing-root', { emulatorEnabled: true, sdk: 'missing-sdk' }, () => {}, { emulatorEnabled: false });
  assert.throws(() => { runtime.emulatorEnabled = true; }, TypeError);
  assert.equal(await runtime.online(), false);
  assert.equal((await runtime.status()).running, false);
  assert.equal(await runtime.adbServerIdle(), false);
  assert.throws(() => runtime.adb(['devices']), /disabled/);
  assert.throws(() => runtime.stopAdbServer(), /disabled/);
  await assert.rejects(runtime.startEmulator(), /disabled/);
  await assert.rejects(runtime.stopEmulator(), /disabled/);
  await assert.rejects(runtime.ensure(), /disabled/);
  assert.throws(() => runtime.launch({ package: 'com.test.game', activity: 'com.test.game/.Main' }, () => {}), /disabled/);
});

test('Direct setup needs only a licensed independent Windows folder and survives restart', async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-setup-'));
  const oldEnv = process.env.AXRB_DATA_HOME;
  t.after(async () => { if (oldEnv === undefined) delete process.env.AXRB_DATA_HOME; else process.env.AXRB_DATA_HOME = oldEnv; await fs.rm(directory, { recursive: true, force: true }); });
  let saved;
  const options = { directory: path.join(directory, buildProfile.runtimeDirectory), totalMemory: () => 6 * 1024 ** 3, runtime: {}, select: async () => {}, save: async (value, installed) => { saved = value; assert.equal(installed.size, 0); }, changed: () => {} };
  const setup = new DirectSetup(options);
  assert.equal((await setup.check()).phase, 'install');
  await assert.rejects(setup.start({ accepted: false }), /license/);
  await setup.start({ directory, accepted: true });
  assert.equal(saved, options.directory);
  assert.equal(setup.status.phase, 'ready');
  assert.equal(setup.status.emulatorDisabled, true);
  assert.equal((await new DirectSetup(options).check()).phase, 'ready');
  assert.deepEqual(await fs.readdir(saved), ['license-acceptance.json']);
  const lowMemory = new DirectSetup({ ...options, totalMemory: () => 5 * 1024 ** 3,
    select: async () => { throw new Error('Must not modify setup on an unsupported PC'); } });
  assert.equal((await lowMemory.check()).phase, 'unsupported');
  await assert.rejects(lowMemory.start({ accepted: true }), /6 GB of system RAM/);
});

test('Direct setup persists exact custom folders and keeps recommended defaults', async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-setup-folders-'));
  const oldEnv = process.env.AXRB_DATA_HOME;
  t.after(async () => {
    if (oldEnv === undefined) delete process.env.AXRB_DATA_HOME; else process.env.AXRB_DATA_HOME = oldEnv;
    await fs.rm(root, { recursive: true, force: true });
  });
  const recommendedDirectory = path.join(root, 'recommended-runtime');
  const recommendedDownloadDirectory = path.join(root, 'recommended-downloads');
  const runtime = { settings: { downloadDir: recommendedDownloadDirectory } };
  let committed;
  const options = { directory: recommendedDirectory, recommendedDirectory, recommendedDownloadDirectory,
    runtime, totalMemory: () => 6 * 1024 ** 3, select: async () => {},
    save: async value => { committed = { directory: value, settings: { ...runtime.settings } }; }, changed: () => {} };
  const setup = new DirectSetup(options);
  assert.equal(setup.status.directory, recommendedDirectory);
  assert.equal(setup.status.downloadDirectory, recommendedDownloadDirectory);
  for (const invalid of ['', 'relative', 'bad\npath']) {
    await assert.rejects(setup.start({ accepted: true, runtimeDirectory: invalid }), /absolute runtime data folder/);
    await assert.rejects(setup.start({ accepted: true, downloadDirectory: invalid }), /absolute game downloads folder/);
  }
  const selected = path.join(root, 'Custom runtime'), downloads = path.join(root, 'Custom games');
  await setup.start({ accepted: true, runtimeDirectory: selected, downloadDirectory: downloads });
  assert.equal(committed.directory, selected);
  assert.equal(committed.settings.downloadDir, downloads);
  assert.equal(setup.status.recommendedDirectory, recommendedDirectory);
  assert.equal(setup.status.recommendedDownloadDirectory, recommendedDownloadDirectory);
  assert.equal(process.env.AXRB_DATA_HOME, path.join(selected, 'output'));
  assert.ok((await fs.stat(downloads)).isDirectory());
  const reopened = new DirectSetup({ ...options, directory: committed.directory });
  assert.equal((await reopened.check()).phase, 'ready');
  assert.equal(reopened.status.downloadDirectory, downloads);
  const portable = new DirectSetup({ ...options, portableRoot: root });
  await assert.rejects(portable.start({ accepted: true, downloadDirectory: path.dirname(root) }), /Portable mode/);
});

test('Emulatorless diagnostics retain host capture without spawning ADB', async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-diagnostics-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  let calls = 0;
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({ sdk: directory, dataHome: directory, emulatorEnabled: false }), spawnProcess: () => { calls++; throw new Error('Unexpected process'); } });
  await capture.start();
  await capture.tick();
  await capture.stop();
  assert.equal(calls, 0);
  assert.ok(performanceScanArgs('.', {}, { emulatorEnabled: false }).includes('-DisableEmulator'));
  assert.ok(!performanceScanArgs('.', {}, { emulatorEnabled: true }).includes('-DisableEmulator'));
});

test('AI diagnostics cannot use emulator ADB in an Emulatorless session', async () => {
  const { DiagnosticHarness } = await import('../core/ai-tools.mjs');
  let executions = 0;
  const harness = new DiagnosticHarness({ context: () => ({ emulatorEnabled: false, sdk: 'unused', port: 5584 }),
    execute: async () => { executions++; throw new Error('Unexpected command'); } });
  const session = harness.begin();
  await assert.rejects(session.execute('android_list_files', { path: '/' }, new AbortController().signal), /disabled/);
  assert.equal(executions, 0);
});
