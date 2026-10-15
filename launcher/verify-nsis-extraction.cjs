const fs = require('node:fs/promises');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { archive } = require('app-builder-lib/out/targets/archive');
const { configureDifferentialAwareArchiveOptions } = require('app-builder-lib/out/targets/differentialUpdateInfoBuilder');
const { getMakeNsisPath, getNsisPluginsPath } = require('app-builder-lib/out/toolsets/windows');
const run = promisify(execFile);
const quote = value => '"' + value.replaceAll('$', '$$').replaceAll('"', '$\\"') + '"';

// Modern 7-Zip extraction is not a sufficient test: use the installer's actual
// plugin and compare bytes, since unsupported archive filters can exit zero.
module.exports = async context => {
  if (process.platform !== 'win32') throw new Error('NSIS payload verification requires a Windows build host.');
  const config = context.packager.config;
  const libraries = path.join(context.appOutDir, 'resources/runtime/out/direct');
  const parent = path.resolve(__dirname, '../out/distribution');
  const work = await fs.mkdtemp(path.join(parent, 'nsis-extraction-'));
  const payload = path.join(work, 'guest.7z');
  await archive('7z', payload, libraries, configureDifferentialAwareArchiveOptions({ withoutDir: true }));
  const compiler = await getMakeNsisPath(config.toolsets?.nsis, config.nsis?.customNsisBinary);
  const plugins = await getNsisPluginsPath(config.toolsets?.nsis, config.nsis?.customNsisResources);
  const extracted = path.join(work, 'extracted');
  const executable = path.join(work, 'extract.exe');
  const script = path.join(work, 'extract.nsi');
  await fs.writeFile(script, [
    'Unicode true', 'RequestExecutionLevel user', 'SilentInstall silent',
    `OutFile ${quote(executable)}`,
    `!addplugindir /x86-unicode ${quote(path.join(plugins, 'x86-unicode'))}`,
    'Section', `SetOutPath ${quote(extracted)}`,
    `Nsis7z::Extract ${quote(payload)}`, 'SectionEnd', '',
  ].join('\r\n'));
  await run(compiler.path, ['/V2', script], { env: { ...process.env, ...compiler.env }, windowsHide: true });
  await run(executable, [], { windowsHide: true });
  async function files(directory, prefix = '') {
    const result = [];
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
      const name = path.join(prefix, entry.name);
      if (entry.isDirectory()) result.push(...await files(path.join(directory, entry.name), name));
      else result.push(name);
    }
    return result;
  }
  for (const name of await files(libraries)) {
    const expected = await fs.readFile(path.join(libraries, name));
    const actual = await fs.readFile(path.join(extracted, name)).catch(() => null);
    if (!expected.length || !actual?.equals(expected)) {
      throw new Error(`NSIS failed to extract Direct runtime file ${name}. Diagnostic files: ${work}`);
    }
  }
  console.log('NSIS extraction verified: the Direct runner, AOSP bridges, bootstrap and guest libraries match the packaged payload.');
};
