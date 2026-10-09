import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { exportGamesZip, gameExportFilename } from '../core/game-export.mjs';
import { safeName } from '../core/download.mjs';
import { importGameArchive, importGameZip } from '../core/game-files.mjs';

async function workspace(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-export-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  return root;
}
const inspect = async file => {
  const identity = JSON.parse(await fs.readFile(file, 'utf8'));
  return { id: `local:${identity.package}`, source: 'local', name: identity.package, apk: file, ...identity };
};

test('ZIP suggestions accept display titles without weakening asset validation', () => {
  assert.equal(gameExportFilename('Batman: Arkham Shadow'), 'Batman- Arkham Shadow.zip');
  assert.equal(gameExportFilename('CON'), 'Game-CON.zip');
  assert.equal(gameExportFilename(' ... '), 'AXRB-game.zip');
  assert.equal(gameExportFilename('日本語'), '日本語.zip');
  for (const title of ['a/b\\c:*?"<>|', 'NUL.txt', 'Game. ', '\x00test', '😀'.repeat(300), undefined]) {
    assert.doesNotThrow(() => safeName(gameExportFilename(title)));
  }
  assert.throws(() => safeName('Batman: Arkham Shadow'));
});
async function game(root, pkg) {
  const dir = path.join(root, pkg); await fs.mkdir(dir);
  const apk = path.join(dir, 'patched.apk'), split = path.join(dir, 'split.apk'), asset = path.join(dir, 'asset');
  await fs.writeFile(apk, JSON.stringify({ package: pkg, versionCode: '7', split: '', patched: true }));
  await fs.writeFile(split, JSON.stringify({ package: pkg, versionCode: '7', split: 'arm64' }));
  await fs.writeFile(asset, 'asset payload');
  return { ...await inspect(apk), files: [
    { kind: 'apk', path: path.join(dir, 'unavailable-original.apk') },
    { kind: 'split', path: split },
    { kind: 'asset', path: asset, destination: `Android/data/${pkg}/files/asset.bundle` }
  ] };
}

test('single export round-trips through the existing ZIP format using the active patched APK', async t => {
  const root = await workspace(t), source = await game(root, 'com.example.one'), output = path.join(root, 'one.zip');
  await fs.writeFile(output, 'previous export');
  const progress = [];
  await exportGamesZip([source], output, inspect, { progress: (...args) => progress.push(args) });
  const imported = await importGameZip(output, root, inspect, { reserve: 0 });
  assert.equal(imported.package, source.package);
  assert.equal(imported.patched, true);
  assert.deepEqual(imported.files.map(f => f.kind).sort(), ['apk', 'asset', 'split']);
  const asset = imported.files.find(f => f.kind === 'asset');
  assert.equal(asset.destination, source.files[2].destination);
  assert.equal(await fs.readFile(asset.path, 'utf8'), 'asset payload');
  assert.equal(progress.at(-1)[0], progress.at(-1)[1]);
});

test('multiple games round-trip with distinct splits and asset destinations', async t => {
  const root = await workspace(t), sources = await Promise.all(['com.example.one', 'com.example.two'].map(pkg => game(root, pkg)));
  const output = path.join(root, 'collection.zip');
  await exportGamesZip(sources, output, inspect);
  const imported = await importGameArchive(output, root, inspect, { reserve: 0 });
  assert.deepEqual(imported.map(g => g.package), sources.map(g => g.package));
  for (const entry of imported) {
    assert.equal(entry.files.length, 3);
    assert.equal(entry.files.find(f => f.kind === 'asset').destination, `Android/data/${entry.package}/files/asset.bundle`);
  }
});

test('mismatched splits and missing assets fail without replacing an existing output', async t => {
  const root = await workspace(t), source = await game(root, 'com.example.one'), output = path.join(root, 'existing.zip');
  await fs.writeFile(output, 'keep');
  await fs.writeFile(source.files[1].path, JSON.stringify({ package: source.package, versionCode: '8', split: 'arm64' }));
  await assert.rejects(exportGamesZip([source], output, inspect), /split APKs/);
  source.files = [source.files[2]];
  await fs.rm(source.files[0].path);
  await assert.rejects(exportGamesZip([source], output, inspect), /ENOENT/);
  assert.equal(await fs.readFile(output, 'utf8'), 'keep');
});

test('cancellation removes partial files and preserves the existing ZIP', async t => {
  const root = await workspace(t), source = await game(root, 'com.example.one'), output = path.join(root, 'existing.zip');
  await fs.writeFile(output, 'keep');
  await fs.writeFile(source.files[2].path, Buffer.alloc(1024 * 1024));
  const controller = new AbortController();
  await assert.rejects(exportGamesZip([source], output, inspect, { signal: controller.signal, progress: () => controller.abort() }), /abort/i);
  assert.equal(await fs.readFile(output, 'utf8'), 'keep');
  assert.equal((await fs.readdir(root)).some(name => name.endsWith('.partial')), false);
});

test('collections validate every game before returning and clean up invalid imports', async t => {
  const root = await workspace(t), sources = await Promise.all(['com.example.one', 'com.example.two'].map(pkg => game(root, pkg)));
  const output = path.join(root, 'collection.zip');
  await exportGamesZip(sources, output, inspect);
  const downloads = path.join(root, 'downloads');
  await assert.rejects(importGameArchive(output, downloads, async file => {
    const identity = await inspect(file);
    if (identity.package === 'com.example.two') throw new Error('Invalid APK');
    return identity;
  }, { reserve: 0 }), /Invalid APK/);
  assert.deepEqual(await fs.readdir(path.join(downloads, 'imports')), []);
});
