import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { MetaSession } from '../core/session.mjs';

async function profile(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-session-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  return directory;
}

test('portable sessions remain readable after copying without Windows encryption', async t => {
  const root = await profile(t), original = path.join(root, 'original'), copied = path.join(root, 'copied');
  await fs.mkdir(original);
  const safeStorage = { isEncryptionAvailable() { throw new Error('Windows encryption must not be used'); } };
  const session = new MetaSession(original, { portable: true, safeStorage });
  await session.save('portable-session-fixture');
  assert.equal(await fs.readFile(path.join(original, 'meta-session.txt'), 'utf8'), 'portable-session-fixture');
  await fs.cp(original, copied, { recursive: true });
  await fs.rm(original, { recursive: true });
  assert.equal(await new MetaSession(copied, { portable: true, safeStorage }).load(), 'portable-session-fixture');
});

test('signing out removes both session formats without removing the library', async t => {
  const directory = await profile(t);
  const session = new MetaSession(directory, { portable: true });
  await session.save('portable-session-fixture');
  await fs.writeFile(path.join(directory, 'meta-session.bin'), 'old encrypted session');
  await fs.writeFile(path.join(directory, 'library.json'), '{}');
  await session.clear();
  assert.deepEqual(await fs.readdir(directory), ['library.json']);
  assert.equal(await session.load(), '');
  await session.clear();
});

test('nonportable mode never falls back to plaintext when encryption is unavailable', async t => {
  const directory = await profile(t);
  await fs.writeFile(path.join(directory, 'meta-session.txt'), 'portable-session-fixture');
  const session = new MetaSession(directory, { safeStorage: { isEncryptionAvailable: () => false } });
  assert.equal(await session.load(), '');
  await assert.rejects(session.save('new-session-fixture'), /encryption is unavailable/);
  assert.deepEqual(await fs.readdir(directory), ['meta-session.txt']);
  assert.equal(await fs.readFile(path.join(directory, 'meta-session.txt'), 'utf8'), 'portable-session-fixture');
});
