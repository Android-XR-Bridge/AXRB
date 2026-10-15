import fs from 'node:fs/promises';
import path from 'node:path';
import { downloadFile, checkSpace, safeName } from './download.mjs';
import { officialDownload, verify, exists } from './setup.mjs';
import { assetDestination } from './game-files.mjs';
import { run } from './runtime.mjs';
import directRuntimeFiles from './direct-runtime-files.cjs';

export async function checkDirectRuntime(root) {
  for (const name of directRuntimeFiles) {
    const file = await fs.stat(path.join(root, name)).catch(error => {
      if (error.code === 'ENOENT') return null;
      throw error;
    });
    if (!file?.isFile() || file.size === 0) {
      throw new Error(`Direct mode installation is incomplete: ${name} is missing or empty. Reinstall the AXRB launcher.`);
    }
  }
}

// Direct mode runs the game's arm64 code in an ordinary Windows process
// (out/direct/axrb-direct.exe) instead of the Android emulator. Its Android
// userspace includes the stock AOSP arm64 ART, framework and system libraries from
// the SDK system image below, which is downloaded here under the Android SDK
// license the player accepted in setup, and unpacked into an Android system
// root by direct/tools/make_sysroot.py.
export const directSystemImage = {
  id: 'direct-system', name: 'Android 16 ARM64 system libraries',
  url: 'https://dl.google.com/android/repository/sys-img/android/arm64-v8a-36_r02.zip',
  size: 810736863, sha1: '62ad6714df790f89c8a8ad32552ffe20bb16fe87',
};
// Bump when make_sysroot.py changes what it writes.
const SYSROOT_FORMAT = 2;
export const directSystemFiles = ['system/bin/app_process64', 'system/lib64/libandroid.so',
  'system/framework/framework.jar', 'system/framework/framework-res.apk',
  'system/etc/classpaths/bootclasspath.pb', 'apex/com.android.art/lib64/libart.so',
  'apex/com.android.art/javalib/core-oj.jar', 'apex/com.android.i18n/javalib/core-icu4j.jar'];

export function directPaths(root, runtimeDirectory) {
  const data = path.join(runtimeDirectory, 'direct');
  return {
    executable: path.join(root, 'out/direct/axrb-direct.exe'),
    script: path.join(root, 'scripts/run/run_direct_game.ps1'),
    data, sysroot: path.join(data, 'sysroot'),
  };
}

export async function directSystemReady(paths) {
  try {
    const receipt = JSON.parse(await fs.readFile(path.join(paths.sysroot, '.axrb-sysroot.json'), 'utf8'));
    if (receipt.digest !== directSystemImage.sha1 || receipt.format !== SYSROOT_FORMAT) return false;
    for (const name of directSystemFiles) {
      const stat = await fs.stat(path.join(paths.sysroot, name));
      if (!stat.isFile() || !stat.size) return false;
    }
    return true;
  } catch { return false; }
}

// Downloads the system image once and builds the system root from it; the
// 810 MB archive is deleted afterwards. update(stage, progress) reports.
export async function ensureDirectSystem(root, paths, { signal, update = () => {}, execute = run } = {}) {
  if (await directSystemReady(paths)) return;
  const downloads = path.join(paths.data, 'downloads');
  await fs.mkdir(downloads, { recursive: true });
  await checkSpace(paths.data, directSystemImage.size + 6 * 1024 ** 3);
  const archive = path.join(downloads, `${directSystemImage.id}.zip`);
  if (!await verify(archive, directSystemImage)) {
    await fs.rm(archive, { force: true });
    update(`Downloading ${directSystemImage.name}`, { completed: 0, total: directSystemImage.size });
    await downloadFile({ ...directSystemImage, destination: archive, signal, validate: officialDownload,
      progress: (completed, total) => update(`Downloading ${directSystemImage.name}`, { completed, total }) });
    if (!await verify(archive, directSystemImage)) throw new Error(`${directSystemImage.name}: download failed verification.`);
  }
  signal?.throwIfAborted();
  update('Preparing Android ART and framework');
  const staging = `${paths.sysroot}.new`;
  await fs.rm(staging, { recursive: true, force: true });
  await fs.rm(`${staging}.system.img`, { force: true });
  await execute('python', [path.join(root, 'direct/tools/make_sysroot.py'), archive, staging, '--art'], { timeout: 20 * 60 * 1000, signal });
  await fs.writeFile(path.join(staging, '.axrb-sysroot.json'), JSON.stringify({ digest: directSystemImage.sha1, format: SYSROOT_FORMAT }));
  await fs.rm(paths.sysroot, { recursive: true, force: true });
  await fs.rename(staging, paths.sysroot);
  await fs.rm(archive, { force: true });
}

// Direct mode reads the game where the launcher keeps it: the OVRPort-patched
// APK (or the store APK, for games whose compatibility profile provides the
// legacy Oculus driver its loader expects), and every expansion file at its Android path (Android/obb/<package>/...).
// Files for the app's own external storage (Android/data/<package>/..., such
// as saves pulled from a Quest) are the game's to change, so they are copied
// into its writable storage instead (see seedExternalStorage).
export function directLaunchFiles(game, compatibility = {}) {
  if (!game?.apk) throw new Error('Download or import the APK first.');
  if (!game.patched && !compatibility.legacyOculusDriver) throw new Error('Direct mode runs the OVRPort-patched APK. Patch the game with ovrport first.');
  const assets = [], external = [];
  for (const file of (game.files || []).filter(file => !['apk', 'split'].includes(file.kind))) {
    const destination = file.destination || `Android/obb/${game.package}/${safeName(file.name)}`;
    assetDestination(game.package, destination);
    (destination.startsWith('Android/data/') ? external : assets).push({ guest: destination, host: file.path });
  }
  if ((game.files || []).some(file => file.kind === 'split')) throw new Error('Direct mode does not run split APKs yet.');
  return { apk: game.apk, assets, external };
}

// Copies imported app-storage files to <data>/sdcard/<Android path> where the
// game finds them. A file already there is the game's own (it may have saved
// since) and is kept.
export async function seedExternalStorage(data, files) {
  for (const file of files) {
    if (!await exists(file.host)) throw new Error(`Content file is missing: ${file.host}. Download the game again.`);
    const target = path.join(data, 'sdcard', ...file.guest.split('/'));
    if (await exists(target)) continue;
    await fs.mkdir(path.dirname(target), { recursive: true });
    const partial = `${target}.axrb-copy`;
    await fs.copyFile(file.host, partial);
    await fs.rename(partial, target);
  }
}

export async function writeAssetList(file, assets) {
  for (const asset of assets) {
    if (!await exists(asset.host)) throw new Error(`Content file is missing: ${asset.host}. Download the game again.`);
    if (/[\t\r\n]/.test(asset.guest + asset.host)) throw new Error('Unsupported content file name.');
  }
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, assets.map(asset => `${asset.guest}\t${asset.host}\n`).join(''), 'utf8');
}
