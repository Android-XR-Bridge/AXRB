import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { crc32 } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { existsSync, readdirSync } from 'node:fs';
import { Runtime, run } from '../core/runtime.mjs';
import { extractZip } from '../core/archive.mjs';
import { elfExports, selectBridgeEntry, parseBridgeManifest, prepareBridge, bridgeEnvironment } from '../core/quest-bridge.mjs';

function elf(symbol = 'ANativeActivity_onCreate') {
  const names = Buffer.from(`\0${symbol}\0`), b = Buffer.alloc(304 + names.length);
  b.write('7f454c46', 0, 'hex'); b[4] = 2; b[5] = 1; b.writeUInt16LE(183, 18);
  b.writeBigUInt64LE(64n, 40); b.writeUInt16LE(64, 58); b.writeUInt16LE(3, 60);
  b.writeUInt32LE(11, 128 + 4); b.writeBigUInt64LE(256n, 128 + 24); b.writeBigUInt64LE(48n, 128 + 32);
  b.writeUInt32LE(2, 128 + 40); b.writeBigUInt64LE(24n, 128 + 56);
  b.writeBigUInt64LE(304n, 192 + 24); b.writeBigUInt64LE(BigInt(names.length), 192 + 32);
  b.writeUInt32LE(1, 280); b[284] = 0x12; b.writeUInt16LE(1, 286); names.copy(b, 304);
  return b;
}
async function zip(file, entries) {
  const local = [], central = []; let offset = 0;
  for (const [name, content] of entries) {
    const n = Buffer.from(name), b = Buffer.from(content), h = Buffer.alloc(30), c = Buffer.alloc(46);
    h.writeUInt32LE(0x04034b50); h.writeUInt16LE(20, 4); h.writeUInt32LE(crc32(b), 14);
    h.writeUInt32LE(b.length, 18); h.writeUInt32LE(b.length, 22); h.writeUInt16LE(n.length, 26);
    c.writeUInt32LE(0x02014b50); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6); c.writeUInt32LE(crc32(b), 16);
    c.writeUInt32LE(b.length, 20); c.writeUInt32LE(b.length, 24); c.writeUInt16LE(n.length, 28); c.writeUInt32LE(offset, 42);
    local.push(h, n, b); central.push(c, n); offset += h.length + n.length + b.length;
  }
  const c = Buffer.concat(central), end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10); end.writeUInt32LE(c.length, 12); end.writeUInt32LE(offset, 16);
  await fs.writeFile(file, Buffer.concat([...local, c, end]));
}
test('ARM64 discovery uses defined exports and rejects corrupt bounds/ABI', () => {
  assert.deepEqual([...elfExports(elf())], ['ANativeActivity_onCreate']);
  const undefinedSymbol = elf(); undefinedSymbol.writeUInt16LE(0, 286); assert.equal(elfExports(undefinedSymbol).size, 0);
  const corrupt = elf(); corrupt.writeBigUInt64LE(999999n, 40); assert.throws(() => elfExports(corrupt), /bounds/);
  const x86 = elf(); x86.writeUInt16LE(62, 18); assert.throws(() => elfExports(x86), /ARM64/);
});
test('engine rules work across package names and reject ambiguous or Java-only apps', () => {
  const native = new Set(['ANativeActivity_onCreate']);
  assert.equal(selectBridgeEntry(new Map([['libcustom.so', native]])).library, 'libcustom.so');
  assert.equal(selectBridgeEntry(new Map([['libunity.so', new Set(['JNI_OnLoad'])]])).engine, 'unity');
  assert.throws(() => selectBridgeEntry(new Map()), /Java\/Dex/);
  const libraries = new Map([['liba.so', native], ['libb.so', native]]);
  assert.throws(() => selectBridgeEntry(libraries), /Multiple/);
  assert.equal(selectBridgeEntry(libraries, { 'android.app.lib_name': 'b' }).library, 'libb.so');
});
test('manifest preserves metadata and environment isolates debug patches and credentials', () => {
  const parsed = parseBridgeManifest("package: name='com.example.game' versionCode='9007199254740993'", ' E: meta-data\n  A: http://schemas.android.com/apk/res/android:name(0x01010003)="android.app.lib_name"\n  A: http://schemas.android.com/apk/res/android:value(0x01010024)="game"');
  assert.equal(parsed.versionCode, '9007199254740993'); assert.equal(parsed.metadata['android.app.lib_name'], 'game');
  const env = bridgeEnvironment({ PATH: 'ok', QB_RET: 'patch', ACCESS_TOKEN: 'secret', XR_RUNTIME_JSON: 'runtime.json' }, { root: 'game', package: parsed.package, engine: 'unity' }, 'event');
  assert.equal(env.QB_RET, undefined); assert.equal(env.ACCESS_TOKEN, undefined); assert.equal(env.QB_JIT, '1');
  assert.equal(env.XR_RUNTIME_JSON, 'runtime.json'); assert.equal(env.QB_PLATFORM, 'standin');
});
test('preparation merges splits, caches content, keeps saves across updates and rejects mismatches', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-bridge-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const bin = path.join(dir, 'out/quest-bridge/Release'), sdk = path.join(dir, 'sdk');
  await fs.mkdir(bin, { recursive: true }); await fs.mkdir(path.join(sdk, 'build-tools/36.0.0'), { recursive: true });
  await fs.writeFile(path.join(sdk, 'build-tools/36.0.0/aapt2.exe'), 'fixture');
  await fs.writeFile(path.join(bin, 'qb-test.exe'), 'fixture'); await fs.writeFile(path.join(bin, 'libz.so'), 'zlib');
  const apk = path.join(dir, 'base.apk'), split = path.join(dir, 'split.apk'), asset = path.join(dir, 'main.7.com.example.game.obb');
  await zip(apk, [['assets/data', 'base'], ['lib/arm64-v8a/libconfig.so', '{"setting":true}']]); await zip(split, [['lib/arm64-v8a/libcustom.so', elf()]]); await fs.writeFile(asset, 'expansion');
  const game = { package: 'com.example.game', apk, files: [{ kind: 'split', path: split }, { kind: 'obb', path: asset, name: path.basename(asset) }] };
  let version = '7';
  const args = { root: dir, directory: path.join(dir, 'profile'), sdk, game, execute: async (_, argv) => argv[1] === 'badging' ? `package: name='com.example.game' versionCode='${version}'` : '' };
  const first = await prepareBridge(args);
  assert.equal(await fs.readFile(path.join(first.root, 'data/app/com.example.game/lib/arm64/libconfig.so'), 'utf8'), '{"setting":true}');
  assert.match(await fs.readFile(path.join(first.root, 'proc/meminfo'), 'utf8'), /MemTotal:\s+8388608 kB/);
  assert.equal(await fs.readFile(path.join(first.root, 'sys/devices/system/cpu/possible'), 'utf8'), '0-5\n');
  assert.equal((await fs.readFile(path.join(first.root, 'proc/cpuinfo'), 'utf8')).match(/processor\t:/g).length, 6);
  assert.match(await fs.readFile(path.join(first.root, 'props.txt'), 'utf8'), /\[ro.product.manufacturer\]: \[Oculus\]/);
  assert.equal(first.engine, 'native'); assert.equal(await fs.readFile(path.join(first.root, 'sdcard/Android/obb/com.example.game', path.basename(asset)), 'utf8'), 'expansion');
  const save = path.join(first.root, 'data/data/com.example.game/save'); await fs.writeFile(save, 'progress');
  assert.equal((await prepareBridge(args)).root, first.root);
  await fs.rm(path.join(first.root, 'proc/meminfo'));
  await prepareBridge(args);
  assert.match(await fs.readFile(path.join(first.root, 'proc/meminfo'), 'utf8'), /MemTotal:/);
  await zip(apk, [['assets/data', 'updated']]); const updated = await prepareBridge(args);
  assert.notEqual(updated.root, first.root); assert.equal(await fs.readFile(path.join(updated.root, 'data/data/com.example.game/save'), 'utf8'), 'progress');
  await assert.rejects(prepareBridge({ ...args, execute: async (_, argv) => argv[1] === 'badging' ? `package: name='com.example.game' versionCode='${argv[2] === split ? '8' : '7'}'` : '' }), /Split APK/);
  await zip(split, [['lib/arm64-v8a/libcustom.so', elf()], ['assets/data', 'conflict']]);
  await assert.rejects(prepareBridge(args), /Conflicting/);
  assert.equal(await fs.readFile(save, 'utf8'), 'progress');
  // Move a valid cached profile while the original no longer exists.
  await zip(split, [['lib/arm64-v8a/libcustom.so', elf()]]);
  const moved = path.join(dir, 'moved-profile'); await fs.rename(args.directory, moved);
  const relocated = await prepareBridge({ ...args, directory: moved });
  assert.equal(await fs.readFile(path.join(relocated.root, 'data/data/com.example.game/save'), 'utf8'), 'progress');
});

const sourceRoot = fileURLToPath(new URL('../..', import.meta.url));
const runner = path.join(sourceRoot, 'out/quest-bridge/Release/qb-test.exe');
test('built native runner launches through Runtime and stops without ADB', { skip: process.platform !== 'win32' || !existsSync(runner), timeout: 40000 }, async t => {
  let text = '', code, ended = false;
  const runtime = new Runtime(sourceRoot, {}, chunk => { text += chunk; });
  runtime.adb = async () => { throw new Error('Bridge must not call ADB'); };
  const finished = new Promise(resolve => {
    runtime.launch({ id: 'fixture', package: 'org.axrb.bridge.test' }, result => { code = result; ended = true; resolve(); }, {}, {
      bridge: { executable: runner, root: path.dirname(runner), library: 'liblifecycletest.so', entry: 'ANativeActivity_onCreate', package: 'org.axrb.bridge.test', engine: 'native' },
    });
  });
  t.after(() => { if (!ended) runtime.child?.kill(); });
  const deadline = Date.now() + 5000;
  while (!text.includes('ANativeActivity_onCreate returned') && !ended && Date.now() < deadline) await new Promise(r => setTimeout(r, 20));
  assert.match(text, /ANativeActivity_onCreate returned/);
  assert.equal(runtime.game, 'fixture'); assert.equal(runtime.fpsHudEvent, null);
  await runtime.stop(); await finished;
  assert.equal(code, 0, text); assert.equal(runtime.child, null);
  assert.match(text, /lifecycle shutdown completed/);
  assert.match(text, /2 passed, 0 failed/);
});

const sdk = process.env.ANDROID_HOME || path.join(process.env.LOCALAPPDATA || '', 'Android/Sdk');
const tools = path.join(sdk, 'build-tools');
const aapt = existsSync(tools) ? readdirSync(tools).sort((a, b) => b.localeCompare(a, undefined, { numeric: true })).map(v => path.join(tools, v, 'aapt2.exe')).find(existsSync) : null;
const androidJar = path.join(sdk, 'platforms/android-29/android.jar');
test('real aapt2 metadata and ARM64 APK prepare in a Unicode profile', { skip: !aapt || !existsSync(androidJar) || !existsSync(runner), timeout: 20000 }, async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-bridge-ü-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const manifest = path.join(dir, 'AndroidManifest.xml'), apk = path.join(dir, 'fixture.apk');
  await fs.writeFile(manifest, `<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.axrb.bridge.fixture" android:versionCode="1">
    <uses-sdk android:minSdkVersion="29"/><application android:hasCode="false">
    <meta-data android:name="fixture.number" android:value="42"/><meta-data android:name="fixture.boolean" android:value="true"/>
    <activity android:name="android.app.NativeActivity"><meta-data android:name="android.app.lib_name" android:value="lifecycletest"/></activity>
    </application></manifest>`);
  await run(aapt, ['link', '-o', apk, '-I', androidJar, '--manifest', manifest]);
  const unpacked = path.join(dir, 'unpacked'); await extractZip(apk, unpacked);
  const entries = [];
  for (const file of await fs.readdir(unpacked)) entries.push([file, await fs.readFile(path.join(unpacked, file))]);
  entries.push(['lib/arm64-v8a/liblifecycletest.so', await fs.readFile(path.join(path.dirname(runner), 'liblifecycletest.so'))]);
  await zip(apk, entries);
  const prepared = await prepareBridge({ root: sourceRoot, directory: path.join(dir, 'profile'), sdk, game: { apk, package: 'org.axrb.bridge.fixture' }, execute: run });
  assert.equal(prepared.entry, 'ANativeActivity_onCreate');
  assert.equal(prepared.metadata['fixture.number'], '42'); assert.equal(prepared.metadata['fixture.boolean'], 'true');
  const output = await run(runner, [path.join(prepared.root, prepared.library), 'ANativeActivity_onCreate']);
  assert.match(output, /ANativeActivity_onCreate returned/);
});
