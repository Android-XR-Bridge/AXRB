import fs from 'node:fs/promises';
import { constants, lstatSync, mkdirSync, realpathSync } from 'node:fs';
import path from 'node:path';
import { checkSpace, safeName } from './download.mjs';

function resolvedDestination(file) {
  try { return realpathSync(file); }
  catch (error) {
    if (error.code !== 'ENOENT' || lstatSync(file, { throwIfNoEntry: false })?.isSymbolicLink()) throw error;
    const parent = path.dirname(file);
    if (parent === file) throw error;
    return path.join(resolvedDestination(parent), path.basename(file));
  }
}
function inside(root, file) {
  const relative = path.relative(realpathSync(root), resolvedDestination(path.resolve(file)));
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}
export function portableOutput(root, file) {
  if (root && (!path.isAbsolute(file) || !inside(root, file))) throw new Error('Portable mode keeps output inside its folder. Choose a location within the portable folder.');
  return file;
}

export function configurePortable(app, root) {
  if (!root) return;
  const temporary = path.join(root, 'temp'), data = path.join(root, 'data');
  const logs = path.join(data, 'logs'), crashes = path.join(data, 'crash-dumps');
  for (const directory of [temporary, data, logs, crashes]) mkdirSync(portableOutput(root, directory), { recursive: true });
  for (const name of ['TEMP', 'TMP', 'TMPDIR']) process.env[name] = temporary;
  process.env.AXRB_PORTABLE_ROOT = root;
  process.env.ANDROID_SDK_HOME = data;
  process.env.ELECTRON_LOG_FILE = path.join(logs, 'chromium.log');
  if (process.env.AXRB_CAPTURE_PREFIX) {
    const captures = path.join(data, 'captures');
    mkdirSync(portableOutput(root, captures), { recursive: true });
    process.env.AXRB_CAPTURE_PREFIX = path.join(captures, 'frame');
  }
  app.setPath('temp', temporary);
  app.setPath('userData', data);
  app.setPath('sessionData', data);
  app.setAppLogsPath(logs);
  app.setPath('crashDumps', crashes);
  for (const [name, value] of [['user-data-dir', data], ['disk-cache-dir', path.join(data, 'cache')],
    ['log-file', process.env.ELECTRON_LOG_FILE], ['crash-dumps-dir', crashes]]) {
    app.commandLine.removeSwitch(name);
    app.commandLine.appendSwitch(name, portableOutput(root, value));
  }
  process.chdir(root);
}

// Windows prunes its own %TEMP%, but a portable folder has no such janitor, so
// scratch left by earlier runs would grow on the drive forever. The single
// instance lock means no other AXRB is using this folder while the sweep runs.
export async function sweepPortableTemp(root, maxAgeMs = 7 * 24 * 60 * 60 * 1000) {
  if (!root) return 0;
  const temporary = path.join(root, 'temp'), cutoff = Date.now() - maxAgeMs;
  let entries;
  try { entries = await fs.readdir(portableOutput(root, temporary)); }
  catch (error) { if (error.code === 'ENOENT') return 0; throw error; }
  let removed = 0;
  for (const entry of entries) {
    const file = path.join(temporary, entry);
    try {
      // lstat and rm both act on the link itself, so a junction planted in temp
      // is unlinked rather than followed out of the portable folder.
      if ((await fs.lstat(file)).mtimeMs >= cutoff) continue;
      await fs.rm(file, { recursive: true, force: true });
      removed += 1;
    } catch { /* an entry still locked or already gone is retried next launch */ }
  }
  return removed;
}

// External imports are read-only sources, not dependencies left on another drive.
export async function carryPortableFiles(root, downloadDir, files, signal) {
  if (!root) return files;
  portableOutput(root, downloadDir);
  const external = files.filter(file => !inside(root, file));
  if (!external.length) return files;
  const sizes = await Promise.all(external.map(async file => (await fs.stat(file)).size));
  await checkSpace(downloadDir, sizes.reduce((total, size) => total + size, 0));
  await fs.mkdir(downloadDir, { recursive: true });
  const directory = await fs.mkdtemp(path.join(downloadDir, 'import-'));
  const copied = new Map();
  try {
    for (const [index, file] of external.entries()) {
      signal?.throwIfAborted();
      const name = path.basename(file);
      // Android resolves expansion files by their exact filename, so an import
      // keeps it and gets its own subfolder instead of colliding with a sibling.
      const folder = path.join(directory, String(index));
      await fs.mkdir(folder);
      const target = path.join(folder, safeName(name, `Windows cannot store the file name "${name}", so it cannot be copied into the portable folder.`));
      await fs.copyFile(file, target, constants.COPYFILE_EXCL);
      copied.set(file, target);
    }
    signal?.throwIfAborted();
    return files.map(file => copied.get(file) || file);
  } catch (error) { await fs.rm(directory, { recursive: true, force: true }); throw error; }
}

export async function removeManagedImportFiles(game, downloadDir) {
  if (!path.isAbsolute(downloadDir)) return false;
  const base = path.resolve(downloadDir);
  const candidates = [game.sourceApk, game.apk, ...(game.files || []).map(file => file.path)];
  const roots = new Set();
  for (const candidate of candidates) {
    if (typeof candidate !== 'string' || !path.isAbsolute(candidate)) continue;
    const relative = path.relative(base, path.resolve(candidate));
    if (!relative || relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)) continue;
    const parts = relative.split(path.sep);
    let rootParts;
    if (/^import-.+/.test(parts[0]) && /^\d+$/.test(parts[1] || '') && parts.length >= 3) {
      rootParts = parts.slice(0, 1);
    } else if (parts[0] === 'imports' && /^[0-9a-f-]{36}$/i.test(parts[1] || '') && parts.length >= 3) {
      rootParts = parts.slice(0, 2);
    } else if (parts[0] === 'quest' && parts[1] === game.package &&
      /^[0-9a-f-]{36}$/i.test(parts[2] || '') && parts.length >= 4) {
      rootParts = parts.slice(0, 3);
    } else continue;
    roots.add(path.join(base, ...rootParts));
  }
  const safeRoots = [];
  for (const root of roots) {
    let current = base;
    let directory = true;
    for (const part of path.relative(base, root).split(path.sep)) {
      current = path.join(current, part);
      let info;
      try { info = await fs.lstat(current); }
      catch (error) { if (error.code === 'ENOENT') { directory = false; break; } throw error; }
      if (!info.isDirectory() || info.isSymbolicLink()) throw new Error('Refusing to delete an imported folder outside AXRB-managed storage.');
    }
    if (directory) safeRoots.push(root);
  }
  for (const root of safeRoots) await fs.rm(root, { recursive: true, force: true });
  return safeRoots.length > 0;
}
