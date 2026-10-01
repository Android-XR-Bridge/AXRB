import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { carryPortableFiles, portableOutput, removeManagedImportFiles, sweepPortableTemp } from '../core/portable.mjs';
import { State } from '../core/state.mjs';

async function fixture(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-portable-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  const root = path.join(directory, 'Portable'), external = path.join(directory, 'Portable-external');
  await fs.mkdir(root); await fs.mkdir(external);
  return { directory, root, external, downloads: path.join(root, 'downloads') };
}

test('portable outputs reject external paths and junction escapes before writing', async t => {
  const { root, external, downloads } = await fixture(t);
  assert.equal(portableOutput(root, path.join(downloads, 'new/file.apk')), path.join(downloads, 'new/file.apk'));
  assert.throws(() => portableOutput(root, path.join(external, 'file.apk')), /inside/);
  const junction = path.join(root, 'redirected');
  await fs.symlink(external, junction, 'junction');
  assert.throws(() => portableOutput(root, path.join(junction, 'new/file.apk')), /inside/);
  assert.deepEqual(await fs.readdir(external), []);
  assert.equal(portableOutput('', path.join(external, 'file.apk')), path.join(external, 'file.apk'));
});

test('portable imported APKs and assets survive removing originals and moving the folder', async t => {
  const { directory, root, external, downloads } = await fixture(t);
  const apk = path.join(external, 'base.apk'), asset = path.join(external, 'main.obb');
  await fs.writeFile(apk, 'APK bytes'); await fs.writeFile(asset, 'asset bytes');
  const copied = await carryPortableFiles(root, downloads, [apk, asset]);
  assert.equal(await fs.readFile(apk, 'utf8'), 'APK bytes');
  assert.equal(await fs.readFile(asset, 'utf8'), 'asset bytes');
  assert.deepEqual(await carryPortableFiles(root, downloads, copied), copied, 'already carried files are not copied again');
  const state = new State(path.join(root, 'data')); await state.load({ portableRoot: root });
  state.put({ id: 'local:com.game', apk: copied[0], files: [{ path: copied[1] }] }); await state.save();
  const moved = path.join(directory, 'Moved');
  await fs.rename(root, moved); await fs.rm(external, { recursive: true });
  const reopened = new State(path.join(moved, 'data')); await reopened.load({ portableRoot: moved });
  assert.equal(await fs.readFile(reopened.data.games[0].apk, 'utf8'), 'APK bytes');
  assert.equal(await fs.readFile(reopened.data.games[0].files[0].path, 'utf8'), 'asset bytes');
});

test('portable imports keep colliding expansion filenames exactly as Android expects', async t => {
  const { root, external, downloads } = await fixture(t);
  await fs.mkdir(path.join(external, 'base')); await fs.mkdir(path.join(external, 'update'));
  const first = path.join(external, 'base/main.1.com.game.obb'), second = path.join(external, 'update/main.1.com.game.obb');
  await fs.writeFile(first, 'base obb'); await fs.writeFile(second, 'update obb');
  const copied = await carryPortableFiles(root, downloads, [first, second]);
  assert.deepEqual(copied.map(file => path.basename(file)), ['main.1.com.game.obb', 'main.1.com.game.obb']);
  assert.equal(await fs.readFile(copied[0], 'utf8'), 'base obb');
  assert.equal(await fs.readFile(copied[1], 'utf8'), 'update obb');
});

test('cancelled portable imports remove their partial copy and keep the source', async t => {
  const { root, external, downloads } = await fixture(t), controller = new AbortController();
  const source = path.join(external, 'base.apk'); await fs.writeFile(source, 'source APK');
  const copy = fs.copyFile;
  t.mock.method(fs, 'copyFile', async (...args) => { await copy(...args); controller.abort(); });
  await assert.rejects(carryPortableFiles(root, downloads, [source], controller.signal), { name: 'AbortError' });
  assert.deepEqual(await fs.readdir(downloads), []);
  assert.equal(await fs.readFile(source, 'utf8'), 'source APK');
  assert.deepEqual(await carryPortableFiles('', downloads, [source]), [source], 'nonportable imports remain user-managed');
});

test('removing imports deletes only recognized AXRB-managed folders', async t => {
  const { external, downloads } = await fixture(t);
  const apkDirectory = path.join(downloads, 'import-abc123', '0');
  await fs.mkdir(apkDirectory, { recursive: true });
  const apk = path.join(apkDirectory, 'base.apk');
  await fs.writeFile(apk, 'copied APK');
  const assetDirectory = path.join(downloads, 'import-def456', '0');
  await fs.mkdir(assetDirectory, { recursive: true });
  const asset = path.join(assetDirectory, 'main.obb');
  await fs.writeFile(asset, 'copied asset');
  assert.equal(await removeManagedImportFiles({ apk, files: [{ path: asset }] }, downloads), true, 'legacy imports without provenance are cleaned up too');
  assert.equal(await fs.stat(path.join(downloads, 'import-abc123')).then(() => true, () => false), false);
  assert.equal(await fs.stat(path.join(downloads, 'import-def456')).then(() => true, () => false), false);

  const zipDirectory = path.join(downloads, 'imports', '12345678-1234-1234-1234-123456789abc');
  await fs.mkdir(zipDirectory, { recursive: true });
  const zipApk = path.join(zipDirectory, 'base.apk');
  await fs.writeFile(zipApk, 'ZIP APK');
  assert.equal(await removeManagedImportFiles({ importedFrom: 'zip', apk: zipApk }, downloads), true);
  assert.equal(await fs.stat(zipDirectory).then(() => true, () => false), false);

  const questDirectory = path.join(downloads, 'quest', 'com.example.game', '12345678-1234-1234-1234-123456789abc');
  await fs.mkdir(questDirectory, { recursive: true });
  const questApk = path.join(questDirectory, 'base.apk');
  await fs.writeFile(questApk, 'Quest APK');
  assert.equal(await removeManagedImportFiles({ importedFrom: 'quest', package: 'com.example.game', apk: questApk }, downloads), true);
  assert.equal(await fs.stat(questDirectory).then(() => true, () => false), false);

  const original = path.join(external, 'user.apk');
  await fs.writeFile(original, 'user-owned APK');
  assert.equal(await removeManagedImportFiles({ importedFrom: 'apk', apk: original }, downloads), false);
  assert.equal(await fs.readFile(original, 'utf8'), 'user-owned APK');
  assert.equal(await removeManagedImportFiles({ importedFrom: 'zip', apk: path.join(external, 'imports', '12345678-1234-1234-1234-123456789abc', 'base.apk') }, downloads), false);
});

test('removing imports refuses managed-looking paths redirected outside downloads', async t => {
  const { external, downloads } = await fixture(t);
  const importRoot = path.join(external, '12345678-1234-1234-1234-123456789abc');
  await fs.mkdir(importRoot, { recursive: true });
  const apk = path.join(importRoot, 'base.apk');
  await fs.writeFile(apk, 'do not remove');
  await fs.mkdir(downloads, { recursive: true });
  await fs.symlink(external, path.join(downloads, 'imports'), 'junction');
  await assert.rejects(removeManagedImportFiles({
    importedFrom: 'zip', apk: path.join(downloads, 'imports', '12345678-1234-1234-1234-123456789abc', 'base.apk')
  }, downloads), /Refusing to delete/);
  assert.equal(await fs.readFile(apk, 'utf8'), 'do not remove');
});

test('the portable temp sweep reclaims stale scratch and leaves a live run alone', async t => {
  const { root } = await fixture(t), temporary = path.join(root, 'temp');
  const stale = path.join(temporary, 'emulator-crash'), fresh = path.join(temporary, 'this-run');
  await fs.mkdir(stale, { recursive: true }); await fs.mkdir(fresh, { recursive: true });
  await fs.writeFile(path.join(stale, 'scratch.bin'), 'abandoned');
  await fs.writeFile(path.join(fresh, 'scratch.bin'), 'in use');
  const old = new Date(Date.now() - 30 * 24 * 60 * 60 * 1000);
  await fs.utimes(stale, old, old);
  assert.equal(await sweepPortableTemp(root), 1);
  assert.deepEqual(await fs.readdir(temporary), ['this-run']);
  assert.equal(await fs.readFile(path.join(fresh, 'scratch.bin'), 'utf8'), 'in use');
});

test('a stale junction in portable temp is unlinked instead of followed outside', async t => {
  const { root, external } = await fixture(t), temporary = path.join(root, 'temp');
  await fs.mkdir(temporary, { recursive: true });
  await fs.writeFile(path.join(external, 'keep.bin'), 'external data');
  const junction = path.join(temporary, 'redirected');
  await fs.symlink(external, junction, 'junction');
  const old = new Date(Date.now() - 30 * 24 * 60 * 60 * 1000);
  await fs.lutimes(junction, old, old);
  assert.equal(await sweepPortableTemp(root), 1);
  assert.deepEqual(await fs.readdir(temporary), []);
  assert.equal(await fs.readFile(path.join(external, 'keep.bin'), 'utf8'), 'external data');
});

test('the portable temp sweep is a no-op before the folder exists and outside portable mode', async t => {
  const { root, external } = await fixture(t);
  assert.equal(await sweepPortableTemp(root), 0, 'a missing temp folder is not an error');
  await fs.writeFile(path.join(external, 'keep.bin'), 'external data');
  assert.equal(await sweepPortableTemp(''), 0);
  assert.deepEqual(await fs.readdir(external), ['keep.bin']);
});
