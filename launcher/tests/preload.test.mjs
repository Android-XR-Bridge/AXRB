import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

async function bridge() {
  let api;
  const calls = [];
  const electron = {
    contextBridge: { exposeInMainWorld: (name, value) => { assert.equal(name, 'axrb'); api = value; } },
    ipcRenderer: { invoke: async (...args) => { calls.push(args); return { ok: true, value: { host: {}, android: null } }; } }
  };
  vm.runInNewContext(await fs.readFile(new URL('../preload.cjs', import.meta.url), 'utf8'), {
    require: name => { assert.equal(name, 'electron'); return electron; }
  });
  return { api, calls };
}

test('storage usage is exposed and forwarded to its main-process handler', async () => {
  const { api, calls } = await bridge();
  assert.equal(typeof api.storageInfo, 'function');
  assert.equal((await api.storageInfo()).ok, true);
  assert.deepEqual(calls, [['axrb:storageInfo']]);
  assert.equal(typeof api.storage, 'function');
});

test('every static renderer API call is exposed by the real preload', async () => {
  const { api } = await bridge();
  const root = new URL('../ui/', import.meta.url);
  for (const file of await fs.readdir(root, { recursive: true })) {
    if (!/\.(jsx|js)$/.test(file)) continue;
    const text = await fs.readFile(new URL(file.replaceAll('\\', '/'), root), 'utf8');
    for (const match of text.matchAll(/\bcall\(['"]([A-Za-z][A-Za-z0-9]*)['"]/g)) {
      assert.equal(typeof api[match[1]], 'function', `${file}: ${match[1]} is missing from preload`);
    }
  }
});
