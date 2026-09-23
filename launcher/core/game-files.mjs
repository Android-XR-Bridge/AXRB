import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { archivePath, extractZip, zipSize } from './archive.mjs';
import { checkSpace, safeName } from './download.mjs';

// Android's install failure codes, in words a player can act on. The code stays
// at the end of the message so a report can still be searched for it.
const INSTALL_FAILURES = {
  INSTALL_FAILED_UPDATE_INCOMPATIBLE: 'An installed copy of this game is signed differently, for example the unpatched store version before patching. Android will not update it with this APK. Uninstall the game first (this deletes its Android data and saves), then install again.',
  INSTALL_FAILED_VERSION_DOWNGRADE: 'An installed copy of this game is newer than this APK. Uninstall the game first (this deletes its Android data and saves) to install this version.',
  INSTALL_FAILED_INSUFFICIENT_STORAGE: 'Android does not have enough free storage for this APK. Free space or grow Android storage in Settings, then retry.',
  INSTALL_FAILED_NO_MATCHING_ABIS: 'This APK has no native code Android can run here (it needs arm64-v8a or x86_64).',
  INSTALL_PARSE_FAILED_NO_CERTIFICATES: 'This APK is not signed. If it was patched, patch it again; otherwise download or import it again.',
  INSTALL_FAILED_INVALID_APK: 'Android rejected this APK as invalid. Download, import or patch it again.',
  INSTALL_PARSE_FAILED_NOT_APK: 'This file is not a valid APK. Download, import or patch it again.',
};
export function describeInstallFailure(error) {
  const code = String(error?.message ?? '').match(/\b(INSTALL_(?:FAILED|PARSE_FAILED)_[A-Z_]+)\b/)?.[1];
  if (!code || !INSTALL_FAILURES[code]) return error;
  return new Error(`${INSTALL_FAILURES[code]} (Android: ${code})`, { cause: error });
}

export const shellQuote =value => `'${String(value).replaceAll("'", "'\\''")}'`;

export function assetDestination(packageName, relative) {
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(packageName)) throw new Error('Invalid package');
  if (typeof relative !== 'string') throw new Error('Missing asset path');
  archivePath(path.resolve('asset-check'), relative);
  const parts = relative.split('/');
  if (parts.some(p => !p || p === '.')) throw new Error('Invalid asset path');
  if (parts[0] !== 'Android' || !['obb', 'data'].includes(parts[1]) || parts[2] !== packageName || parts.length < 4) throw new Error('Asset path does not belong to this game.');
  return `/sdcard/${relative}`;
}

export function classifyAsset(name, packageName) {
  const parts = name.split('/');
  // Allow one enclosing game folder or sdcard prefix, but retain the Android layout.
  const android = parts.indexOf('Android');
  if (android >= 0) {
    const destination = parts.slice(android).join('/');
    assetDestination(packageName, destination);
    return destination;
  }
  const area = parts.findIndex(p => p === 'obb' || p === 'data');
  if (area >= 0) {
    const destination = `Android/${parts.slice(area).join('/')}`;
    assetDestination(packageName, destination); return destination;
  }
  const base = parts.at(-1);
  if (/\.obb$/i.test(base)) {
    if (!new RegExp(`^(?:main|patch)\\.\\d+\\.${packageName.replaceAll('.', '\\.')}\\.obb$`, 'i').test(base)) throw new Error(`OBB does not match ${packageName}: ${base}`);
    return `Android/obb/${packageName}/${safeName(base)}`;
  }
  throw new Error(`Cannot determine where ${name} belongs. Place assets under Android/obb/${packageName} or Android/data/${packageName}.`);
}

export async function walkFiles(directory, prefix = '') {
  const files = [];
  for (const entry of await fs.readdir(path.join(directory, prefix), { withFileTypes: true })) {
    const name = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isSymbolicLink()) throw new Error('Linked files are not supported.');
    if (entry.isDirectory()) files.push(...await walkFiles(directory, name));
    else if (entry.isFile()) files.push(name);
  }
  return files;
}

export async function fileRecord(file, name, kind, destination, signal) {
  const hash = createHash('sha256'), handle = await fs.open(file);
  try { for await (const chunk of handle.createReadStream()) { signal?.throwIfAborted(); hash.update(chunk); } }
  finally { await handle.close(); }
  return { path: file, name, kind, ...(destination ? { destination } : {}), size: (await fs.stat(file)).size, sha256: hash.digest('hex') };
}

export async function inspectGameFolder(directory, inspect, { signal, update = () => {} } = {}) {
  const names = await walkFiles(directory), apks = [];
  for (const name of names.filter(n => /\.apk$/i.test(n) && !n.split('/').some(p => ['Android', 'obb', 'data'].includes(p)))) {
    signal?.throwIfAborted(); update(`Reading ${name}`);
    apks.push({ name, metadata: await inspect(path.join(directory, name), { allowSplit: true }) });
  }
  const bases = apks.filter(a => !a.metadata.split);
  if (bases.length !== 1) throw new Error('ZIP must contain exactly one base APK, plus any matching split APKs.');
  const game = bases[0].metadata, splitNames = new Set();
  for (const apk of apks) {
    if (apk.metadata.package !== game.package || apk.metadata.versionCode !== game.versionCode || splitNames.has(apk.metadata.split)) throw new Error('APK splits must belong to the same game and version.');
    splitNames.add(apk.metadata.split);
  }
  const files = [], destinations = new Set();
  for (const name of names) {
    signal?.throwIfAborted();
    if (name.startsWith('__MACOSX/') || name.endsWith('/.DS_Store') || name === '.DS_Store') continue;
    const apk = apks.find(a => a.name === name);
    const destination = apk ? undefined : classifyAsset(name, game.package);
    if (destination && destinations.has(destination.toLowerCase())) throw new Error('Multiple assets target the same file.');
    if (destination) destinations.add(destination.toLowerCase());
    update(`Verifying ${name}`);
    files.push(await fileRecord(path.join(directory, name), name, apk ? (apk.metadata.split ? 'split' : 'apk') : 'asset', destination, signal));
  }
  return { ...game, apk: path.join(directory, bases[0].name), files, downloaded: true };
}

export async function importGameZip(file, downloadDir, inspect, { signal, update = () => {}, progress = () => {}, reserve } = {}) {
  const directory = path.join(downloadDir, 'imports', randomUUID());
  try {
    update('Checking ZIP');
    const size = await zipSize(file, directory, { signal });
    await checkSpace(directory, size, reserve === undefined ? {} : { reserve });
    update('Extracting ZIP');
    await extractZip(file, directory, { signal, limit: 256 * 1024 ** 3, progress });
    return await inspectGameFolder(directory, inspect, { signal, update });
  } catch (error) { await fs.rm(directory, { recursive: true, force: true }); throw error; }
}

async function pushAsset(file, adb, signal, progress) {
  const controller = new AbortController();
  const monitorSignal = signal ? AbortSignal.any([signal, controller.signal]) : controller.signal;
  const remote = shellQuote(file.remote);
  const command = `if [ -e ${remote} ]; then stat -c '%i:%s:%y:%z' ${remote}; else echo missing; fi`;
  const sample = async () => {
    try {
      const text = (await adb(['shell', command], { timeout: 2000, signal: monitorSignal })).trim();
      return text === 'missing' || /^\d+:\d+:/.test(text) ? text : null;
    } catch { return null; } // Progress queries must not fail the actual transfer.
  };
  const initial = await sample();
  const monitor = (async () => {
    let started = false, reported = 0;
    while (initial !== null && !monitorSignal.aborted) {
      try { await delay(1000, undefined, { signal: monitorSignal }); } catch { break; }
      const current = await sample();
      if (monitorSignal.aborted) break;
      const match = current?.match(/^\d+:(\d+):/);
      // A reinstall's old file is not progress. Wait for replacement or a write.
      if (!match || !(started ||= current !== initial)) continue;
      const size = Number(match[1]);
      if (!Number.isSafeInteger(size)) continue;
      const copied = Math.min(file.size, size);
      if (copied > reported) { reported = copied; progress(copied); }
    }
  })();
  try {
    await adb(['push', file.path, file.remote], { timeout: 60 * 60 * 1000, signal });
  } finally {
    controller.abort();
    await monitor;
  }
}

export async function installFiles(game, adb, update = () => {}, signal) {
  const splits = (game.files || []).filter(f => f.kind === 'split');
  const assets = (game.files || []).filter(f => !['apk', 'split'].includes(f.kind)).map(file => ({ ...file,
    remote: assetDestination(game.package, file.destination || `Android/obb/${game.package}/${safeName(file.name)}`) }));
  if (!game.apk) throw new Error('Import or download an APK first.');
  const apkPaths = [game.apk, ...splits.map(f => f.path)];
  let apkBytes = 0;
  for (const file of apkPaths) apkBytes += (await fs.stat(file)).size;
  let bytes = apkBytes;
  for (const file of assets) { file.size = (await fs.stat(file.path)).size; bytes += file.size; }
  let completed = 0;
  const report = (stage, current = completed) => update(stage, { completed: current, total: bytes });
  const disk = await adb(['shell', 'df', '-k', '/data'], { signal });
  const available = disk.trim().split(/\r?\n/).at(-1)?.trim().split(/\s+/)[3];
  if (!/^\d+$/.test(available || '')) throw new Error('Could not check Android free space.');
  if (Number(available) * 1024 < bytes + 1024 ** 3) throw new Error(`Android needs at least ${(bytes / 1024 ** 3 + 1).toFixed(1)} GB free for this installation. Free space and retry.`);
  const rootAdb = assets.length && (await adb(['shell', 'id', '-u'], { signal })).trim() === '0';
  signal?.throwIfAborted(); update('Installing APK');
  await adb([splits.length ? 'install-multiple' : 'install', '--no-incremental', '--force-queryable', '-r', ...apkPaths], { timeout: 30 * 60 * 1000, signal })
    .catch(error => { throw describeInstallFailure(error); });
  completed = apkBytes;
  let uid;
  if (rootAdb) {
    uid = (await adb(['shell', `stat -c %u ${shellQuote(`/data/user/0/${game.package}`)}`], { signal })).trim();
    if (!/^\d+$/.test(uid) || Number(uid) < 10000) throw new Error('Cannot determine app UID for asset ownership.');
  }
  for (const file of assets) {
    signal?.throwIfAborted(); report(`Copying ${file.name}`);
    await adb(['shell', `mkdir -p ${shellQuote(path.posix.dirname(file.remote))}`], { signal });
    await pushAsset(file, adb, signal, copied => {
      // Reserve completion until ADB acknowledges the copy and final sync succeeds.
      report(`Copying ${file.name}`, Math.min(completed + copied, bytes - 1));
    });
    if (rootAdb) {
      // Root ADB leaves nested directories inaccessible to the app. Change only
      // this file and its ancestors through the package directory, never siblings.
      const parts = file.remote.split('/'), targets = [];
      while (parts.length >= 5) { targets.push(shellQuote(parts.join('/'))); parts.pop(); }
      await adb(['shell', `chown ${uid} ${targets.join(' ')}`], { signal });
    }
    completed += file.size;
  }
  update('Finishing installation…');
  await adb(['shell', 'sync'], { signal });
  report('Installed');
}
