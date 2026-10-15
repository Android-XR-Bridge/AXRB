const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');
const directRuntimeFiles = require('./core/direct-runtime-files.cjs');

module.exports = async (_context, packagedDirectory) => {
  await fs.access(path.join(__dirname, 'assets/quest-catalog.jar'));
  const root = packagedDirectory
    ? path.join(packagedDirectory, 'resources/runtime')
    : path.resolve(__dirname, '../out/distribution/resources/runtime');
  const manifest = JSON.parse(await fs.readFile(path.join(root, 'distribution.json'), 'utf8'));
  // New installations need the complete Direct runtime, not just whichever
  // files happened to be left in the staging tree by an earlier build.
  for (const name of [...directRuntimeFiles, 'tools/aapt2/aapt2.exe', 'tools/aapt2/NOTICE', 'scripts/run/android_tools.py']) {
    if (!manifest.sha256[name]) throw new Error(`Direct runtime missing from release: ${name}. Rebuild with scripts/build/installer.ps1.`);
  }
  for (const [name, expected] of Object.entries(manifest.sha256)) {
    if (createHash('sha256').update(await fs.readFile(path.join(root, name))).digest('hex') !== expected) throw new Error(`Runtime staging is incomplete or changed: ${name}. Run prepare-release.mjs again.`);
  }
  await fs.access(path.join(root, 'tools/python/python.exe'));
  const layer = JSON.parse(await fs.readFile(path.join(root, 'out/gpu/Release/axrb_gpu_layer.json'), 'utf8'));
  if (layer.layer.library_path !== '.\\axrb_gpu_layer.dll') throw new Error('GPU layer path is not portable on the pinned emulator.');
};
