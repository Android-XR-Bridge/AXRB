import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { checkDirectRuntime, directLaunchFiles, directPaths, directSystemReady, ensureDirectSystem, directSystemImage, directSystemFiles, seedExternalStorage, writeAssetList } from '../core/direct.mjs';
import directRuntimeFiles from '../core/direct-runtime-files.cjs';

test('Direct mode rejects incomplete installations before launching or downloading', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-payload-'));
  try {
    for (const name of directRuntimeFiles) {
      await fs.mkdir(path.dirname(path.join(root, name)), { recursive: true });
      await fs.writeFile(path.join(root, name), 'payload');
    }
    await checkDirectRuntime(root);
    for (const name of directRuntimeFiles) {
      const file = path.join(root, name);
      await fs.unlink(file);
      await assert.rejects(checkDirectRuntime(root), error => error.message.includes(name) && /Reinstall/.test(error.message));
      await fs.writeFile(file, '');
      await assert.rejects(checkDirectRuntime(root), /missing or empty/);
      await fs.writeFile(file, 'payload');
    }
    await checkDirectRuntime(root);
  } finally { await fs.rm(root, { recursive: true, force: true }); }
});

const nexus = {
  package: 'com.Ubisoft.ACNexusVR', patched: true, apk: 'D:\\AXRB\\1\\2\\axrb-patched\\base-axrb.apk',
  files: [
    { name: 'base.apk', path: 'D:\\AXRB\\1\\2\\base.apk', kind: 'apk' },
    { name: 'Master.pck', path: 'D:\\AXRB\\1\\2\\Master.pck', kind: 'obb' },
    { name: 'save.dat', path: 'D:\\import\\save.dat', kind: 'asset', destination: 'Android/data/com.Ubisoft.ACNexusVR/files/save.dat' },
  ],
};

test('direct mode runs the patched APK and maps content files to their Android paths', () => {
  assert.deepEqual(directLaunchFiles(nexus), {
    apk: nexus.apk,
    assets: [
      { guest: 'Android/obb/com.Ubisoft.ACNexusVR/Master.pck', host: 'D:\\AXRB\\1\\2\\Master.pck' },
    ],
    external: [
      { guest: 'Android/data/com.Ubisoft.ACNexusVR/files/save.dat', host: 'D:\\import\\save.dat' },
    ],
  });
});

test('direct mode needs an OVRPort-patched APK', () => {
  assert.throws(() => directLaunchFiles({ ...nexus, patched: false }), /Patch the game with ovrport first/);
  // Games whose profile provides the legacy Oculus driver run their store APK.
  assert.equal(directLaunchFiles({ ...nexus, patched: false }, { legacyOculusDriver: true }).apk, nexus.apk);
  assert.throws(() => directLaunchFiles({ ...nexus, patched: false }, { questIdentity: true }), /Patch the game with ovrport first/);
  assert.throws(() => directLaunchFiles({ ...nexus, apk: '' }), /Download or import the APK first/);
});

test('content files cannot leave the game’s own storage', () => {
  const escaping = { ...nexus, files: [{ name: 'x', path: 'D:\\x', kind: 'asset', destination: 'Android/obb/com.other.Game/x' }] };
  assert.throws(() => directLaunchFiles(escaping), /does not belong to this game/);
});

test('the asset list is one tab-separated line per file and refuses missing files', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-'));
  try {
    const content = path.join(directory, 'Master.pck');
    await fs.writeFile(content, 'x');
    const list = path.join(directory, 'sessions', 'game.assets');
    await writeAssetList(list, [{ guest: 'Android/obb/p.q/Master.pck', host: content }]);
    assert.equal(await fs.readFile(list, 'utf8'), `Android/obb/p.q/Master.pck\t${content}\n`);
    await assert.rejects(writeAssetList(list, [{ guest: 'Android/obb/p.q/gone', host: path.join(directory, 'gone') }]), /Content file is missing/);
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test('app-storage files are copied where the game can change them, never over its own', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-'));
  try {
    const imported = path.join(directory, 'save.dat'), data = path.join(directory, 'direct');
    await fs.writeFile(imported, 'imported');
    const files = [{ guest: 'Android/data/p.q/files/save.dat', host: imported }];
    await seedExternalStorage(data, files);
    const target = path.join(data, 'sdcard', 'Android', 'data', 'p.q', 'files', 'save.dat');
    assert.equal(await fs.readFile(target, 'utf8'), 'imported');
    await fs.writeFile(target, 'played');
    await seedExternalStorage(data, files);
    assert.equal(await fs.readFile(target, 'utf8'), 'played');
    await assert.rejects(seedExternalStorage(data, [{ guest: 'Android/data/p.q/x', host: path.join(directory, 'gone') }]), /Content file is missing/);
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test('the system root is rebuilt only when its receipt does not match the pinned image', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-direct-'));
  try {
    const paths = directPaths('C:\\AXRB\\resources\\runtime', directory);
    assert.equal(paths.sysroot, path.join(directory, 'direct', 'sysroot'));
    assert.equal(await directSystemReady(paths), false);
    await fs.mkdir(paths.sysroot, { recursive: true });
    await fs.writeFile(path.join(paths.sysroot, '.axrb-sysroot.json'), JSON.stringify({ digest: directSystemImage.sha1, format: 1 }));
    assert.equal(await directSystemReady(paths), false, 'old managed-only roots must migrate');
    await fs.writeFile(path.join(paths.sysroot, '.axrb-sysroot.json'), JSON.stringify({ digest: directSystemImage.sha1, format: 2 }));
    assert.equal(await directSystemReady(paths), false, 'a receipt cannot hide an incomplete extraction');
    for (const name of directSystemFiles) {
      const file = path.join(paths.sysroot, name);
      await fs.mkdir(path.dirname(file), { recursive: true });
      await fs.writeFile(file, 'aosp');
    }
    assert.equal(await directSystemReady(paths), true);
    // A ready root needs no download and no Python run.
    await ensureDirectSystem('C:\\AXRB', paths, { execute: () => { throw new Error('must not run'); } });
    await fs.truncate(path.join(paths.sysroot, directSystemFiles[0]), 0);
    assert.equal(await directSystemReady(paths), false);
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});
