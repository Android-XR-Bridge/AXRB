import { run } from './runtime.mjs';

const patchName = /^patch_[a-z0-9_]+$/;
// A cold JVM, or a self-extracting build unpacking to a scanned temp folder,
// takes far longer than a warm run on the maintainer's machine. This is a
// host-side tool, so a timeout here must not read as an Android fault.
const CLI_TIMEOUT = 120000;
const CLI_TIMEOUT_MESSAGE = 'The ovrport CLI did not respond. Check the path in Settings, and that Java is installed if it is a JAR.';

export class Ovrport {
  constructor(execute = run) { this.execute = execute; this.help = new Map(); }
  run(cli, args, options = {}) {
    return this.execute(/\.jar$/i.test(cli) ? 'java' : cli, /\.jar$/i.test(cli) ? ['-jar', cli, ...args] : args, options);
  }
  // A profile's extra patches can be newer than the configured CLI, which
  // would otherwise fail with its own "Unknown patch" error mid-patch.
  async requireProfiles(cli, extraPatches = []) {
    if (!this.help.has(cli)) this.help.set(cli, await this.run(cli, ['patch'], { timeout: CLI_TIMEOUT, requireCompleteOutput: true, timeoutMessage: CLI_TIMEOUT_MESSAGE }));
    if (!this.help.get(cli).includes('--extra-patches=<value>')) throw new Error('This game requires a newer ovrport CLI with compatibility-profile support.');
    if (!extraPatches.length) return;
    const available = new Set((await this.patches(cli)).map(patch => patch.name));
    const missing = extraPatches.filter(name => !available.has(name));
    if (missing.length) throw new Error(`This game's compatibility profile needs ${missing.join(', ')}, which the configured ovrport CLI does not have. Choose a newer ovrport CLI in Settings.`);
  }
  async patches(cli) {
    const output = await this.run(cli, ['patches', '--json'], { timeout: CLI_TIMEOUT, requireCompleteOutput: true, timeoutMessage: CLI_TIMEOUT_MESSAGE });
    let patches;
    try { patches = JSON.parse(output); } catch { throw new Error('Update the ovrport CLI to choose patch options in AXRB.'); }
    const names = new Set();
    if (!Array.isArray(patches) || !patches.length || patches.some(p => {
      if (!p || !patchName.test(p.name) || typeof p.recommended !== 'boolean' || names.has(p.name)) return true;
      names.add(p.name); return false;
    })) throw new Error('The ovrport CLI returned an invalid patch catalog.');
    return patches.map(({ name, recommended }) => ({ name, recommended }));
  }
}

export function selectedPatchArgs(catalog, selected) {
  if (!Array.isArray(selected) || !selected.length) throw new Error('Select at least one patch.');
  const available = new Set(catalog.map(p => p.name)), seen = new Set();
  const values = selected.map(p => {
    if (!p || !available.has(p.name) || seen.has(p.name)) throw new Error(`Invalid or duplicate patch selection: ${p?.name}`);
    seen.add(p.name);
    if (!Array.isArray(p.arguments) || p.arguments.some(a => typeof a !== 'string' || /[;,\r\n\0]/.test(a))) throw new Error(`Invalid arguments for ${p.name}.`);
    return p.name + (p.arguments.length ? `=${p.arguments.join(',')}` : '');
  });
  if (seen.has('patch_vrapi_openxr') && seen.has('patch_remove_vrapi')) throw new Error('patch_vrapi_openxr conflicts with patch_remove_vrapi; select only one.');
  return [`--patches=${values.join(';')}`];
}
