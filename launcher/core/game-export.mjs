import fs from 'node:fs/promises';
import { createReadStream, createWriteStream } from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { pipeline } from 'node:stream/promises';
import { finished } from 'node:stream/promises';
import yazl from 'yazl';
import { archivePath } from './archive.mjs';
import { assetDestination } from './game-files.mjs';

export const COLLECTION_MANIFEST = 'axrb-games.json';

export function gameExportFilename(title) {
  // Display titles can contain punctuation that Windows filenames cannot.
  // This is a suggested name, not an untrusted asset path to validate.
  let name = Array.from(String(title ?? '').replace(/[<>:"/\\|?*\x00-\x1f]/g, '-').trim()).slice(0, 100).join('').replace(/[. ]+$/, '');
  if (!name) name = 'AXRB-game';
  if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(name)) name = `Game-${name}`;
  return `${name}.zip`;
}

export async function exportGamesZip(games, output, inspect, { signal, update = () => {}, progress = () => {} } = {}) {
  if (!Array.isArray(games) || !games.length || games.length > 1000) throw new Error('Select between 1 and 1000 games.');
  const entries = [], packages = new Set(), folders = [];
  for (const [index, game] of games.entries()) {
    signal?.throwIfAborted(); update(`Checking ${game.name}`);
    if (!game.apk) throw new Error(`${game.name}: download or import the APK before exporting.`);
    const base = await inspect(game.apk, { allowSplit: true });
    if (base.split || base.package !== game.package || packages.has(base.package)) throw new Error('Select one complete build per game.');
    assetDestination(base.package, `Android/data/${base.package}/check`);
    packages.add(base.package);
    const folder = `games/${String(index + 1).padStart(4, '0')}`;
    folders.push(folder);
    const prefix = games.length > 1 ? `${folder}/` : '';
    const files = [{ path: game.apk, name: 'base.apk' }];
    const splits = new Set();
    for (const file of game.files || []) {
      if (file.kind === 'apk') continue; // Export the currently selected APK, including patches.
      if (file.kind === 'split') {
        const split = await inspect(file.path, { allowSplit: true });
        if (!split.split || split.package !== base.package || split.versionCode !== base.versionCode || splits.has(split.split)) throw new Error(`${game.name}: split APKs do not match the selected APK.`);
        splits.add(split.split);
        files.push({ path: file.path, name: `split_${splits.size}.apk` });
      } else {
        const name = file.destination || `Android/obb/${base.package}/${path.basename(file.name)}`;
        assetDestination(base.package, name);
        files.push({ path: file.path, name });
      }
    }
    const names = new Set();
    for (const file of files) {
      signal?.throwIfAborted();
      const name = prefix + file.name;
      archivePath(path.dirname(output), name);
      if (names.has(name.toLowerCase())) throw new Error('Multiple files target the same archive entry.');
      names.add(name.toLowerCase());
      if (path.resolve(file.path).toLowerCase() === path.resolve(output).toLowerCase()) throw new Error('Export cannot replace a game file.');
      const stat = await fs.lstat(file.path);
      if (!stat.isFile()) throw new Error(`Missing or linked game file: ${file.name}`);
      entries.push({ ...file, name, size: stat.size, mtime: stat.mtime });
    }
  }
  const total = entries.reduce((sum, entry) => sum + entry.size, 0);
  if (entries.length > 199999 || !Number.isSafeInteger(total) || total > 256 * 1024 ** 3 - 1024 ** 2) throw new Error('Export exceeds the ZIP import limit.');
  const space = await fs.statfs(path.dirname(output));
  if (Number(space.bavail) * Number(space.bsize) < total + entries.length * 1024 + 1024 ** 2) throw new Error('Not enough free space for the game ZIP.');
  const temporary = `${output}.${randomUUID()}.partial`, zip = new yazl.ZipFile();
  const streams = new Set();
  let completed = 0;
  zip.on('error', error => zip.outputStream.destroy(error));
  const writing = pipeline(zip.outputStream, createWriteStream(temporary, { flags: 'wx' }), { signal });
  // Attach rejection handling immediately while entries are being queued.
  writing.catch(() => {});
  try {
    if (games.length > 1) zip.addBuffer(Buffer.from(JSON.stringify({ format: 'axrb-games', version: 1, games: folders })), COLLECTION_MANIFEST);
    for (const entry of entries) zip.addReadStreamLazy(entry.name, { size: entry.size, mtime: entry.mtime, compress: false }, callback => {
      try {
        signal?.throwIfAborted(); update(`Exporting ${entry.name}`);
        const stream = createReadStream(entry.path, { signal });
        streams.add(stream);
        stream.on('close', () => streams.delete(stream));
        stream.on('data', chunk => { completed += chunk.length; progress(completed, total); });
        callback(null, stream);
      } catch (error) { callback(error); }
    });
    zip.end();
    await writing;
    signal?.throwIfAborted();
    await fs.rename(temporary, output);
    return { file: output, games: games.length, bytes: total };
  } finally {
    zip.outputStream.destroy();
    const closing = [...streams].map(stream => { stream.destroy(); return finished(stream).catch(() => {}); });
    await Promise.all(closing);
    await writing.catch(() => {});
    await fs.rm(temporary, { force: true });
  }
}
