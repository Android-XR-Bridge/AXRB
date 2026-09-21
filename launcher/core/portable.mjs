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
    for (const file of external) {
      signal?.throwIfAborted();
      const target = path.join(directory, safeName(path.basename(file)));
      await fs.copyFile(file, target, constants.COPYFILE_EXCL);
      copied.set(file, target);
    }
    signal?.throwIfAborted();
    return files.map(file => copied.get(file) || file);
  } catch (error) { await fs.rm(directory, { recursive: true, force: true }); throw error; }
}
