import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { carryPortableFiles, portableOutput } from '../core/portable.mjs';
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
