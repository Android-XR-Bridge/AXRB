import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { collectDiagnostics, condense, diagnosticSources, redact, uploadDiagnostics, DEFAULT_DIAGNOSTICS_ENDPOINT, DIAGNOSTICS_RETENTION_DAYS } from '../core/diagnostics.mjs';

const identity = { user: 'flori', computer: 'SUPERDUPERGAY', home: 'C:\\Users\\flori' };

test('redaction removes the account name from every path spelling', () => {
  const sample = [
    'SDK=C:\\Users\\flori\\AppData\\Local\\AXRB Runtime\\sdk',
    'image=c:/users/Flori/AppData/Local/Android/Sdk/system.img',
    'Source: C:\\Users\\flori\\Documents\\Coding\\axrb\\scripts\\run.ps1',
    'User: SUPERDUPERGAY\\flori on SUPERDUPERGAY',
  ].join('\n');
  const output = redact(sample, identity);
  assert.doesNotMatch(output, /flori/i, 'no spelling of the account name may survive');
  assert.doesNotMatch(output, /SUPERDUPERGAY/i, 'the machine name must be removed too');
  assert.match(output, /AppData[\\/]Local[\\/]AXRB Runtime[\\/]sdk/, 'the useful part of the path must remain');
  assert.match(output, /<user>|<home>/);
});

test('redaction leaves unrelated words that merely contain the account name', () => {
  const output = redact('florian-pc booted; floristry.apk installed; flori', { user: 'flori', computer: 'box', home: '' });
  assert.match(output, /florian-pc/, 'a longer word must not be cut apart');
  assert.match(output, /floristry\.apk/);
  assert.match(output, /<user>$/, 'a standalone mention is still redacted');
});

test('redaction handles an account name with regex metacharacters', () => {
  const output = redact('C:\\Users\\a.b+c\\logs and a.b+c alone', { user: 'a.b+c', computer: 'box', home: '' });
  assert.doesNotMatch(output, /a\.b\+c/);
  assert.match(output, /C:\\Users\\<user>\\logs/);
});

test('Basic authorization is redacted without hiding ordinary Basic messages', () => {
  const credential = Buffer.from('demo-user:private-password').toString('base64');
  const output = redact([
    `request Authorization: Basic ${credential} status=401`,
    `{"authorization": "Basic ${credential}", "status": 401}`,
    `Proxy-Authorization: bAsIc ${credential}`,
    'Basic graphics support is available',
  ].join('\n'));
  assert.doesNotMatch(output, new RegExp(credential));
  assert.match(output, /request Authorization:.*status=401/);
  assert.match(output, /"status": 401/);
  assert.match(output, /Basic graphics support is available/);
});

test('the bundle carries the useful logs, redacted, and never the game library', async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-diag-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'logs/emulator'), { recursive: true });
  await fs.mkdir(path.join(root, 'logs/game'), { recursive: true });
  await fs.writeFile(path.join(root, 'logs/game/host.err'), 'AXRB OpenXR: failed to load openxr_loader.dll\r\n');
  await fs.writeFile(path.join(root, 'logs/emulator/emulator.stdout.log'), `boot from ${os.homedir()}\\avd\n`);
  const bundle = await collectDiagnostics({
    dataHome: root, version: '0.1.3',
    settings: { avd: 'axrb-managed-api36', port: 5584, sdk: 'C:\\Users\\flori\\AppData\\Local\\AXRB Runtime\\sdk', managedDirectory: 'C:\\Users\\flori\\AppData\\Local\\AXRB Runtime' },
    setupLogs: ['Android startup diagnostic: 31s elapsed; adb=offline'],
    hardware: { gpu: 'AMD Radeon RX 6600', hypervisor: true },
  });
  assert.match(bundle, /failed to load openxr_loader\.dll/, 'the failing host log is the point of the bundle');
  assert.match(bundle, /adb=offline/, 'the setup transcript is included');
  assert.match(bundle, /AMD Radeon RX 6600/);
  assert.match(bundle, /axrb-managed-api36/);
  assert.doesNotMatch(bundle, new RegExp(os.userInfo().username.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'),
    'nothing identifying may reach the bundle');
  assert.doesNotMatch(bundle, /"sdk"|downloadDir|ovrportCli/, 'local paths stay out of the settings summary');
  assert.match(bundle, /guest-vulkan\.json ---\n\(unavailable/, 'missing logs are reported, not fatal');
  assert.doesNotMatch(bundle, /\r/, 'line endings are normalised for the paste view');
});

test('diagnostic sources cover the emulator and host logs a failure needs', () => {
  const names = diagnosticSources('C:\\data').map(([label]) => label);
  assert.deepEqual(names, ['emulator.stdout.log', 'emulator.stderr.log', 'guest-gles.txt', 'guest-vulkan.json', 'host.log', 'host.err', 'session.json']);
});

test('game sessions and performance windows appear as their own bundle sections', async () => {
  const bundle = await collectDiagnostics({
    dataHome: 'Z:\\nowhere', sessions: [
      JSON.stringify({ id: 'ab12de34', package: 'com.meta.samples.NorthStar', exitCode: 3, closeRequested: false, gameProcessLost: true, pauseSucceeded: false, syncSucceeded: false }),
    ],
    perf: 'host-end-frame: rate=75.0/s p50=0.212ms p95=0.322ms p99=0.396ms (12 windows)',
  });
  assert.match(bundle, /--- game sessions ---\n.*com\.meta\.samples\.NorthStar/);
  assert.match(bundle, /"gameProcessLost":true/);
  assert.match(bundle, /--- performance windows ---\nhost-end-frame: rate=75\.0\/s/);
  const bare = await collectDiagnostics({ dataHome: 'Z:\\nowhere' });
  assert.doesNotMatch(bare, /game sessions|performance windows/, 'empty sections stay out of the bundle');
});

test('upload posts the bundle and returns the link the service reports', async () => {
  const calls = [];
  const link = await uploadDiagnostics('bundle contents', {
    fetchImpl: async (url, options) => { calls.push({ url: String(url), options }); return { ok: true, status: 201, text: async () => 'https://dpaste.com/8G54ZT3QS\n' }; },
  });
  assert.equal(link, 'https://dpaste.com/8G54ZT3QS');
  assert.equal(calls[0].url, DEFAULT_DIAGNOSTICS_ENDPOINT);
  assert.equal(calls[0].options.method, 'POST');
  const sent = calls[0].options.body;
  assert.ok(sent instanceof URLSearchParams);
  assert.equal(sent.get('content'), 'bundle contents');
  // An upload that never expires would leave the logs public indefinitely.
  assert.equal(sent.get('expiry_days'), String(DIAGNOSTICS_RETENTION_DAYS));
});

test('a self-hosted endpoint receives the bundle as plain text, not a paste form', async () => {
  const calls = [];
  await uploadDiagnostics('bundle contents', {
    endpoint: 'https://logs.example.com/axrb',
    fetchImpl: async (url, options) => { calls.push(options); return { ok: true, status: 200, text: async () => 'https://logs.example.com/r/1' }; },
  });
  assert.equal(calls[0].body, 'bundle contents');
  assert.match(calls[0].headers['Content-Type'], /text\/plain/);
});

test('upload accepts a self-hosted endpoint that answers with JSON', async () => {
  const link = await uploadDiagnostics('bundle', {
    endpoint: 'https://logs.example.com/axrb',
    fetchImpl: async () => ({ ok: true, status: 201, text: async () => '{"url":"https://logs.example.com/r/8f3a21"}' }),
  });
  assert.equal(link, 'https://logs.example.com/r/8f3a21');
});

test('upload refuses plaintext endpoints and reports service failures', async () => {
  await assert.rejects(uploadDiagnostics('x', { endpoint: 'http://logs.example.com' }), /HTTPS/);
  await assert.rejects(
    uploadDiagnostics('x', { fetchImpl: async () => ({ ok: false, status: 413, text: async () => 'too large' }) }),
    /413/);
  await assert.rejects(
    uploadDiagnostics('x', { fetchImpl: async () => ({ ok: true, status: 200, text: async () => 'rate limited' }) }),
    /usable link/);
});

test('condensing collapses repeated kernel lines but keeps distinct ones', () => {
  const battery = n => `[${n}.200031] healthd: battery l=100 v=5000 st=2`;
  const text = [battery(1), battery(2), battery(3), battery(4), '[5.0] init: starting adbd', battery(6)].join('\n');
  const output = condense(text);
  assert.match(output, /previous line repeated 3 more times/);
  assert.match(output, /init: starting adbd/, 'a distinct line must survive between repeats');
  assert.equal((output.match(/healthd/g) || []).length, 2, 'one representative per run of repeats');
});

test('condensing keeps the most recent lines when a log is long', () => {
  const output = condense(Array.from({ length: 900 }, (_, i) => `line ${i}`).join('\n'), 250);
  assert.match(output, /650 earlier lines omitted/);
  assert.match(output, /line 899$/, 'the tail is what matters after a failure');
  assert.doesNotMatch(output, /line 100\b/);
});
