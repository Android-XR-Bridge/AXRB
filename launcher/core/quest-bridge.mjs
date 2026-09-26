import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { archivePath, extractZip, zipSize } from './archive.mjs';
import { assetDestination } from './game-files.mjs';

// Read exported ELF64 symbols, not strings that might merely mention an entry point.
export function elfExports(bytes) {
  if (bytes.length < 64 || bytes.toString('hex', 0, 4) !== '7f454c46' || bytes[4] !== 2 || bytes[5] !== 1 || bytes.readUInt16LE(18) !== 183) throw new Error('Expected an ARM64 ELF library.');
  const range = (at, size) => {
    if (!Number.isSafeInteger(at) || !Number.isSafeInteger(size) || at < 0 || size < 0 || at + size > bytes.length) throw new Error('Invalid ELF section bounds.');
    return bytes.subarray(at, at + size);
  };
  const offset = Number(bytes.readBigUInt64LE(40)), stride = bytes.readUInt16LE(58), count = bytes.readUInt16LE(60);
  if (stride < 64 || !count) throw new Error('ELF has no supported section table.');
  range(offset, stride * count);
  const section = index => { if (index >= count) throw new Error('Invalid ELF string table.'); return range(offset + index * stride, 64); };
  const data = s => range(Number(s.readBigUInt64LE(24)), Number(s.readBigUInt64LE(32)));
  const names = new Set();
  for (let i = 0; i < count; i++) {
    const s = section(i);
    if (s.readUInt32LE(4) !== 11) continue;
    const symbols = data(s), strings = data(section(s.readUInt32LE(40))), size = Number(s.readBigUInt64LE(56));
    if (size < 24 || symbols.length % size) throw new Error('Invalid ELF symbol table.');
    for (let at = 0; at < symbols.length; at += size) {
      const name = symbols.readUInt32LE(at), binding = symbols[at + 4] >> 4;
      if (!symbols.readUInt16LE(at + 6) || ![1, 2].includes(binding)) continue;
      const end = strings.indexOf(0, name);
      if (name >= strings.length || end < 0) throw new Error('Invalid ELF symbol name.');
      names.add(strings.toString('utf8', name, end));
    }
  }
  return names;
}

export function parseBridgeManifest(badging, xml) {
  const pkg = /^package: name='([^']+)'/m.exec(badging)?.[1];
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(pkg || '')) throw new Error('APK has no valid package identity.');
  const metadata = {};
  let current = null;
  for (const line of xml.split(/\r?\n/)) {
    if (/^\s*E:/.test(line)) {
      if (current?.name && current.value !== undefined) metadata[current.name] = current.value;
      current = /E: meta-data\b/.test(line) ? {} : null;
    } else if (current) {
      const match = /A: .*android:(name|value)(?:\([^)]*\))?=(.*)/.exec(line);
      if (match) {
        const quoted = /^"([^"]*)"/.exec(match[2]);
        const integer = /^\(type 0x(10|12)\)0x([0-9a-f]+)/i.exec(match[2]);
        const scalar = /^(true|false|-?\d+(?:\.\d+)?)(?:\s|$)/.exec(match[2]);
        if (quoted) current[match[1]] = quoted[1];
        else if (integer) current[match[1]] = integer[1] === '12' ? String(parseInt(integer[2], 16) !== 0) : String(parseInt(integer[2], 16));
        else if (scalar) current[match[1]] = scalar[1];
      }
    }
  }
  if (current?.name && current.value !== undefined) metadata[current.name] = current.value;
  return { package: pkg, versionCode: /\bversionCode='([^']*)'/.exec(badging)?.[1] || '', metadata };
}

export function selectBridgeEntry(libraries, metadata = {}) {
  const specified = metadata['android.app.lib_name'];
  if (specified) {
    const library = `lib${specified}.so`, entry = metadata['android.app.func_name'] || 'ANativeActivity_onCreate';
    if (!/^[\w.-]+$/.test(library) || !libraries.get(library)?.has(entry) || entry !== 'ANativeActivity_onCreate') throw new Error(`Unsupported manifest native entry: ${library}:${entry}`);
    return { library, entry, engine: /libUE4|libUnreal/.test(library) ? 'unreal' : 'native' };
  }
  if (libraries.get('libunity.so')?.has('JNI_OnLoad')) return { library: 'libunity.so', entry: 'JNI_OnLoad', engine: 'unity' };
  const candidates = [...libraries].filter(([, symbols]) => symbols.has('ANativeActivity_onCreate'));
  if (candidates.length === 1) return { library: candidates[0][0], entry: 'ANativeActivity_onCreate', engine: /libUE4|libUnreal/.test(candidates[0][0]) ? 'unreal' : 'native' };
  throw new Error(candidates.length ? 'Multiple native activities: an explicit manifest entry is required.' : 'No supported Unity or NativeActivity entry. Java/Dex activities are not implemented by Quest Bridge.');
}

async function digest(file) {
  const hash = createHash('sha256'), handle = await fs.open(file);
  try { for await (const chunk of handle.createReadStream()) hash.update(chunk); } finally { await handle.close(); }
  return hash.digest('hex');
}

export function bridgeEnvironment(base, prepared, stopEvent) {
  // Debug switches can patch guest instructions; never inherit them accidentally.
  const env = Object.fromEntries(Object.entries(base).filter(([key]) => !/^QB_|TOKEN|SECRET|PASSWORD|META_SESSION/i.test(key)));
  return { ...env, QB_ROOT: prepared.root, QB_PACKAGE: prepared.package, QB_JIT: '1', QB_PLATFORM: 'standin',
    QB_XR_RUNTIME: 'active', QB_STOP_EVENT: stopEvent, QB_RUN_SECONDS: '0',
    ...(prepared.engine === 'unity' ? { QB_DRIVE: '2147483647' } : {}) };
}

export async function prepareBridge({ root, directory, sdk, game, execute, onOutput = () => {} }) {
  const executable = path.join(root, 'out/quest-bridge/Release/qb-test.exe');
  try { await fs.access(executable); } catch { throw new Error('Build Quest Bridge first: powershell -File scripts/build/quest_bridge.ps1'); }
  if (!game.apk) throw new Error('Import or download the APK and its content first.');
  const versions = (await fs.readdir(path.join(sdk, 'build-tools'))).sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
  let aapt;
  for (const version of versions) {
    const candidate = path.join(sdk, 'build-tools', version, 'aapt2.exe');
    try { await fs.access(candidate); aapt = candidate; break; } catch {}
  }
  if (!aapt) throw new Error('Android SDK build-tools (aapt2.exe) are required to prepare an APK.');
  const identity = parseBridgeManifest(await execute(aapt, ['dump', 'badging', game.apk]), await execute(aapt, ['dump', 'xmltree', game.apk, '--file', 'AndroidManifest.xml']));
  if (identity.package !== game.package) throw new Error('APK package changed. Import it again.');
  const apks = [game.apk, ...(game.files || []).filter(f => f.kind === 'split').map(f => f.path)];
  for (const apk of apks.slice(1)) {
    const info = parseBridgeManifest(await execute(aapt, ['dump', 'badging', apk]), '');
    if (info.package !== identity.package || info.versionCode !== identity.versionCode) throw new Error('Split APK package/version does not match the base APK.');
  }
  const assets = (game.files || []).filter(f => !['apk', 'split'].includes(f.kind)).map(f => ({ ...f,
    remote: assetDestination(game.package, f.destination || `Android/obb/${game.package}/${f.name}`) }));
  onOutput('Quest Bridge: checking APKs and content checksums.\n');
  const inputs = [];
  for (const file of [...apks.map(p => ({ path: p })), ...assets]) {
    const sha256 = await digest(file.path);
    const expected = file.sha256 || (game.files || []).find(f => f.path === file.path)?.sha256;
    if (expected && expected.toLowerCase() !== sha256) throw new Error('Game content checksum changed. Download it again.');
    inputs.push({ sha256, remote: file.remote || '' });
  }
  const key = createHash('sha256').update(JSON.stringify({ schema: 1, inputs, zlib: await digest(path.join(path.dirname(executable), 'libz.so')) })).digest('hex');
  const home = path.join(directory, identity.package), target = path.join(home, 'builds', key);
  let cached;
  try { cached = JSON.parse(await fs.readFile(path.join(target, 'bridge.json'), 'utf8')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const connectSaves = async location => {
    for (const [guest, save] of [[`data/data/${identity.package}`, 'private'], [`sdcard/Android/data/${identity.package}`, 'external']]) {
      const persistent = path.join(home, 'saves', save), link = path.join(location, guest);
      await fs.mkdir(persistent, { recursive: true });
      await fs.mkdir(path.dirname(link), { recursive: true });
      try {
        const stat = await fs.lstat(link);
        if (!stat.isSymbolicLink()) throw new Error('Expected a save junction; preserving the existing directory.');
        if (path.resolve(await fs.readlink(link)) === path.resolve(persistent)) continue;
        await fs.unlink(link); // Remove only the old link after a portable move.
      } catch (error) { if (error.code !== 'ENOENT') throw error; }
      await fs.symlink(persistent, link, 'junction');
    }
  };
  if (cached) { await connectSaves(target); return { ...cached, root: target, executable }; }
  const stage = path.join(home, 'builds', `.prepare-${randomUUID()}`);
  await fs.mkdir(stage, { recursive: true });
  try {
    let required = 512 * 1024 ** 2;
    for (const apk of apks) required += 2 * await zipSize(apk, stage, { limit: 32 * 1024 ** 3 }) + (await fs.stat(apk)).size;
    for (const asset of assets) required += (await fs.stat(asset.path)).size;
    const space = await fs.statfs(stage);
    if (Number(space.bavail) * Number(space.bsize) < required) throw new Error(`Quest Bridge preparation needs ${(required / 1024 ** 3).toFixed(1)} GB free in the launcher profile.`);
    const apkRoot = path.join(stage, 'apk'), libRoot = path.join(stage, 'data/app', identity.package, 'lib/arm64');
    await fs.mkdir(libRoot, { recursive: true });
    for (const [index, apk] of apks.entries()) {
      onOutput(`Quest Bridge: extracting APK ${index + 1}/${apks.length}.\n`);
      const unpacked = path.join(stage, `unpack-${index}`);
      await extractZip(apk, unpacked, { limit: 32 * 1024 ** 3 });
      // Conflicting split files are rejected, never chosen by archive order.
      const merge = async (from, to) => {
        await fs.mkdir(to, { recursive: true });
        for (const item of await fs.readdir(from, { withFileTypes: true })) {
          const source = path.join(from, item.name), destination = path.join(to, item.name);
          if (item.isDirectory()) await merge(source, destination);
          else {
            try { await fs.copyFile(source, destination, fs.constants.COPYFILE_EXCL); }
            catch (error) { if (error.code !== 'EEXIST' || await digest(source) !== await digest(destination)) throw new Error(`Conflicting APK content: ${item.name}`, { cause: error }); }
          }
        }
      };
      for (const [from, to] of [['assets', path.join(apkRoot, 'assets')], ['lib/arm64-v8a', libRoot]]) {
        try { await fs.access(path.join(unpacked, from)); } catch { continue; }
        await merge(path.join(unpacked, from), to);
      }
      await fs.copyFile(apk, path.join(stage, 'data/app', identity.package, index ? `split-${index}.apk` : 'base.apk'));
      await fs.rm(unpacked, { recursive: true });
    }
    const libraries = new Map();
    for (const name of await fs.readdir(libRoot)) if (name.endsWith('.so')) libraries.set(name, elfExports(await fs.readFile(path.join(libRoot, name))));
    const selected = selectBridgeEntry(libraries, identity.metadata);
    await fs.mkdir(apkRoot, { recursive: true });
    await fs.writeFile(path.join(apkRoot, 'meta-data.txt'), Object.entries(identity.metadata).map(([k, v]) => `${k}=${String(v).replace(/[\r\n]/g, '')}`).join('\n'));
    // The runner's zlib is compiled from the selected NDK; games can supply their own.
    if (!libraries.has('libz.so')) await fs.copyFile(path.join(path.dirname(executable), 'libz.so'), path.join(libRoot, 'libz.so'));
    await connectSaves(stage);
    for (const folder of ['proc/self', 'dev', 'tmp', `sdcard/Android/obb/${identity.package}`]) await fs.mkdir(path.join(stage, folder), { recursive: true });
    for (const file of assets) {
      const destination = archivePath(stage, file.remote.slice(1));
      await fs.mkdir(path.dirname(destination), { recursive: true });
      await fs.copyFile(file.path, destination);
    }
    const prepared = { ...identity, ...selected, library: path.posix.join('data/app', identity.package, 'lib/arm64', selected.library), key };
    await fs.writeFile(path.join(stage, 'bridge.json'), JSON.stringify(prepared, null, 2));
    await fs.rename(stage, target);
    onOutput(`Quest Bridge: ${selected.engine}, ${selected.entry}; separate experimental saves.\n`);
    return { ...prepared, root: target, executable };
  } catch (error) { await fs.rm(stage, { recursive: true, force: true }); throw error; }
}
