import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

// Logs are shared with strangers to get help, so identifying details are
// removed before anything leaves the machine. Windows paths carry the account
// name in every line, which is the detail users least expect to publish.
export function redactionPatterns({ user = os.userInfo().username, computer = os.hostname(), home = os.homedir() } = {}) {
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const patterns = [];
  if (home) patterns.push([new RegExp(escape(home).replaceAll('\\\\', '[\\\\/]'), 'gi'), '<home>']);
  if (user) {
    // Both separators appear: PowerShell transcripts use backslashes, Node and
    // the Android tooling print the same paths with forward slashes.
    patterns.push([new RegExp(`([A-Za-z]:[\\\\/]+Users[\\\\/]+)${escape(user)}(?=[\\\\/]|\\b)`, 'gi'), '$1<user>']);
    patterns.push([new RegExp(`(^|[^A-Za-z0-9_-])${escape(user)}(?![A-Za-z0-9_-])`, 'gi'), '$1<user>']);
  }
  if (computer) patterns.push([new RegExp(`(^|[^A-Za-z0-9_-])${escape(computer)}(?![A-Za-z0-9_-])`, 'gi'), '$1<computer>']);
  return patterns;
}
export function redact(text, options) {
  let value = String(text ?? '');
  // Order matters: the full home directory, then the account name inside a
  // Users path, then any standalone mention, then the machine name.
  for (const [pattern, replacement] of redactionPatterns(options)) value = value.replace(pattern, replacement);
  return value;
}
const KILOBYTE = 1024;
// Kernel logs repeat one status line for hours (healthd alone accounted for
// most of an early bundle), which buries the failure the reader came for.
// Consecutive lines that differ only by their kernel timestamp collapse.
export function condense(text, maxLines = 250) {
  const lines = String(text).split('\n');
  const kept = [];
  let previous = null, repeats = 0;
  const flush = () => { if (repeats) kept.push(`[… previous line repeated ${repeats} more times …]`); repeats = 0; };
  for (const line of lines) {
    const key = line.replace(/^\[\s*\d+\.\d+\]\s*/, '');
    if (previous !== null && key === previous) { repeats++; continue; }
    flush();
    kept.push(line);
    previous = key;
  }
  flush();
  return (kept.length > maxLines ? [`[… ${kept.length - maxLines} earlier lines omitted …]`, ...kept.slice(-maxLines)] : kept).join('\n');
}
async function readTail(file, limit = 128 * KILOBYTE) {
  const handle = await fs.open(file, 'r');
  try {
    const { size } = await handle.stat();
    const length = Math.min(size, limit);
    const buffer = Buffer.alloc(length);
    await handle.read(buffer, 0, length, size - length);
    return (size > length ? `[… ${size - length} earlier bytes omitted …]\n` : '') + buffer.toString('utf8');
  } finally { await handle.close(); }
}
export function diagnosticSources(dataHome) {
  const emulator = path.join(dataHome, 'logs/emulator'), game = path.join(dataHome, 'logs/game');
  return [
    ['emulator.stdout.log', path.join(emulator, 'emulator.stdout.log')],
    ['emulator.stderr.log', path.join(emulator, 'emulator.stderr.log')],
    ['guest-gles.txt', path.join(emulator, 'guest-gles.txt')],
    ['guest-vulkan.json', path.join(emulator, 'guest-vulkan.json')],
    ['host.log', path.join(game, 'host.log')],
    ['host.err', path.join(game, 'host.err')],
  ];
}
// The library file is deliberately absent: it is megabytes of cover art and a
// record of everything the user owns, none of which helps diagnose a failure.
export async function collectDiagnostics({ dataHome, version = '', settings = {}, setupLogs = [], hardware = null, now = () => new Date() } = {}) {
  const sections = [`AXRB diagnostics ${now().toISOString()}`,
    `launcher ${version}; ${process.platform} ${os.release()}; ${os.arch()}; node ${process.versions.node}`];
  if (hardware) sections.push(`hardware ${JSON.stringify(hardware)}`);
  const { managedDirectory, sdk, downloadDir, ovrportCli, ...safeSettings } = settings;
  sections.push(`settings ${JSON.stringify({ ...safeSettings, managed: Boolean(managedDirectory) })}`);
  if (setupLogs.length) sections.push(`--- setup transcript ---\n${setupLogs.join('\n')}`);
  for (const [label, file] of diagnosticSources(dataHome)) {
    try { sections.push(`--- ${label} ---\n${condense(await readTail(file))}`); }
    catch (error) { sections.push(`--- ${label} ---\n(unavailable: ${error.code || error.message})`); }
  }
  return redact(sections.join('\n\n').replaceAll('\r\n', '\n')) + '\n';
}
export const DEFAULT_DIAGNOSTICS_ENDPOINT = 'https://dpaste.com/api/v2/';
export const DIAGNOSTICS_RETENTION_DAYS = 30;
const USER_AGENT = 'AXRB-Launcher';
// The default is a public paste service that expires the upload. A self-hosted
// endpoint instead receives the bundle as a plain-text body and answers with
// either a bare URL or {"url": …}.
export async function uploadDiagnostics(bundle, { endpoint = DEFAULT_DIAGNOSTICS_ENDPOINT, fetchImpl = fetch, title = 'AXRB diagnostics' } = {}) {
  const target = new URL(endpoint);
  if (target.protocol !== 'https:') throw new Error('Diagnostics can only be uploaded over HTTPS.');
  const request = target.href === DEFAULT_DIAGNOSTICS_ENDPOINT
    ? { body: new URLSearchParams({ content: bundle, syntax: 'text', title, expiry_days: String(DIAGNOSTICS_RETENTION_DAYS) }), headers: {} }
    : { body: bundle, headers: { 'Content-Type': 'text/plain; charset=utf-8' } };
  const response = await fetchImpl(target, { method: 'POST', body: request.body, headers: { 'User-Agent': USER_AGENT, ...request.headers } });
  const text = (await response.text()).trim();
  if (!response.ok) throw new Error(`Log upload failed (${response.status}). ${text.slice(0, 200)}`.trim());
  let link = text;
  try { link = JSON.parse(text).url ?? text; } catch {}
  link = String(link).trim().split(/\s+/).at(-1) ?? '';
  if (!/^https:\/\/\S+$/.test(link)) throw new Error('The log service did not return a usable link.');
  return link;
}
