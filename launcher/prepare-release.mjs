import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { extractZip } from './core/archive.mjs';
import { downloadFile } from './core/download.mjs';
import { verify } from './core/setup.mjs';

const launcher = path.dirname(fileURLToPath(import.meta.url)), root = path.dirname(launcher);
const staging = path.join(root, 'out/distribution/resources'), runtime = path.join(staging, 'runtime');
await fs.rm(staging, { recursive: true, force: true });
await fs.mkdir(runtime, { recursive: true });
// Match the OpenXR headers pinned in host/src/CMakeLists.txt. The checksum
// covers Khronos's complete NuGet artifact, not just the extracted desktop DLL.
const openxr = {
  url: 'https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.60/OpenXR.Loader.1.1.60.nupkg',
  size: 1767003, sha256: '93c800cfe3269a19683fe6fef51236c16a164c641a6903eff84c6b2816afe556',
};
const loaderArchive = path.join(root, 'out/distribution/OpenXR.Loader.1.1.60.nupkg');
if (!await verify(loaderArchive, openxr)) await downloadFile({ ...openxr, destination: loaderArchive, validate: value => {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.username || url.password || url.port ||
    !['github.com', 'release-assets.githubusercontent.com'].includes(url.hostname)) throw new Error('Unexpected Khronos download host');
} });
if (!await verify(loaderArchive, openxr)) throw new Error('OpenXR loader package checksum mismatch');
const loaderStaging = path.join(root, 'out/distribution/openxr-loader');
const loaderEntry = 'native/x64/release/bin/openxr_loader.dll';
await fs.rm(loaderStaging, { recursive: true, force: true });
await extractZip(loaderArchive, loaderStaging, { entries: [loaderEntry] });
const files = [
  ['out/distribution/build-host/bin/Release/axrb-host-bridge.exe', 'out/host/bin/Release/axrb-host-bridge.exe'],
  [`out/distribution/openxr-loader/${loaderEntry}`, 'out/host/bin/Release/openxr_loader.dll'],
  ['out/distribution/build-gpu/Release/axrb_gpu_layer.dll', 'out/gpu/Release/axrb_gpu_layer.dll'],
  ['out/distribution/build-clock/Release/axrb_clock_launcher.exe', 'out/clock/Release/axrb_clock_launcher.exe'],
  ['out/distribution/build-clock/Release/axrb_whpx_clock.dll', 'out/clock/Release/axrb_whpx_clock.dll'],
  ['out/distribution/build-clock/Release/MinHook-LICENSE.txt', 'licenses/MinHook.txt'],
  ...['out/android/audio/libaxrb_audio_compat.so', 'out/android/vulkan/libVkLayer_AXRB_runtime.so',
    'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk', 'launcher/inspect_apk.py',
    'scripts/paths.ps1', 'scripts/emulator/windows_android_emulator.ps1', 'scripts/emulator/check_windows.ps1',
    'scripts/emulator/gpu_validation.ps1', 'scripts/emulator/windows_gpu.py',
    'scripts/emulator/android_runtime_policy.py', 'scripts/emulator/audio_policy.py', 'scripts/emulator/distribution.py',
    'scripts/emulator/storage_policy.py', 'scripts/emulator/unreal_memory_policy.py', 'scripts/emulator/qemu_multicore.py',
    'scripts/run/run_windows_game.ps1', 'scripts/run/fps_hud.ps1', 'scripts/run/stop_game.ps1',
    'scripts/run/performance_scan.ps1',
    'modules/performance_overlay/collect_guest_cpu.py', 'modules/performance_overlay/resource_history.py',
    'scripts/run/open_windows_features.ps1', 'scripts/run/android_app_label.py', 'scripts/run/steamvr_app_identity.py'].map(p => [p, p]),
];
const sha256 = {};
for (const [from, to] of files) {
  const bytes = await fs.readFile(path.join(root, from));
  await fs.mkdir(path.dirname(path.join(runtime, to)), { recursive: true });
  await fs.writeFile(path.join(runtime, to), bytes);
  sha256[to] = createHash('sha256').update(bytes).digest('hex');
}
const layer = { file_format_version: '1.2.0', layer: { name: 'VK_LAYER_AXRB_gpu_share', type: 'GLOBAL', library_path: '.\\axrb_gpu_layer.dll', api_version: '1.3.0', implementation_version: '1', description: 'AXRB Windows shared eye textures' } };
await fs.writeFile(path.join(runtime, 'out/gpu/Release/axrb_gpu_layer.json'), JSON.stringify(layer));
await fs.writeFile(path.join(runtime, 'distribution.json'), JSON.stringify({ version: 1, sha256 }, null, 2));
const python = { url: 'https://www.python.org/ftp/python/3.14.3/python-3.14.3-embed-amd64.zip', size: 12023245, sha256: 'ad4961a479dedbeb7c7d113253f8db1b1935586b73c27488712beec4f2c894e6' };
const archive = path.join(root, 'out/distribution/python.zip');
if (!await verify(archive, python)) await downloadFile({ ...python, destination: archive, validate: value => { if (new URL(value).origin !== 'https://www.python.org') throw new Error('Unexpected Python download host'); } });
if (!await verify(archive, python)) throw new Error('Python checksum mismatch');
await extractZip(archive, path.join(runtime, 'tools/python'));
await fs.appendFile(path.join(runtime, 'tools/python/python314._pth'), '\n../../scripts/emulator\n../../scripts/run\n../../launcher\n');
await fs.cp(path.join(launcher, 'licenses'), path.join(staging, 'licenses'), { recursive: true });
await fs.cp(path.join(root, 'licenses'), path.join(staging, 'licenses'), { recursive: true });
await fs.copyFile(path.join(root, 'LICENSE'), path.join(staging, 'licenses/AXRB-LICENSE.txt'));
await fs.copyFile(path.join(root, 'CONTRIBUTING.md'), path.join(staging, 'licenses/CONTRIBUTING.md'));
for (const name of ['bc7e', 'bc7enc']) {
  await fs.copyFile(path.join(root, `runtime/vulkan/texture/${name}/LICENSE`), path.join(staging, `licenses/${name}.txt`));
}
await fs.copyFile(path.join(launcher, 'LICENSE'), path.join(staging, 'licenses/AXRB-launcher-GPL-3.0.txt'));
await fs.copyFile(path.join(launcher, 'THIRD_PARTY_NOTICES.md'), path.join(staging, 'licenses/THIRD_PARTY_NOTICES.md'));
// Preserve the license texts for renderer dependencies, which Vite embeds in JS.
async function licenses(directory) {
  for (const item of await fs.readdir(directory, { withFileTypes: true })) {
    if (item.name.startsWith('.')) continue;
    const file = path.join(directory, item.name);
    if (item.isDirectory()) await licenses(file);
    else if (/^(license|copying|notice)(\.|$)/i.test(item.name)) {
      const target = path.join(staging, 'licenses/npm', path.relative(path.join(launcher, 'node_modules'), file));
      await fs.mkdir(path.dirname(target), { recursive: true }); await fs.copyFile(file, target);
    }
  }
}
await licenses(path.join(launcher, 'node_modules'));
console.log(`Prepared ${staging}`);
