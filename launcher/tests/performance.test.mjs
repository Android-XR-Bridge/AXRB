import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { performanceScanArgs, performanceScanTimeout, SCAN_SCRIPT } from '../core/performance.mjs';

// The launcher passes the repository root, not its own directory: the scripts
// sit beside it, and in a packaged build beside the unpacked runtime.
const root = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const settings = { sdk: 'C:\\Users\\someone\\Android\\Sdk', port: 5580, managedDirectory: 'D:\\AXRB Runtime' };
const value = (args, name) => args[args.indexOf(name) + 1];

test('the script the launcher runs is the one in the repository', async () => {
  // The launcher and a person running it by hand must reach the same file, so
  // a rename has to break here rather than at a user's first bug report.
  await fs.access(path.join(root, SCAN_SCRIPT));
});

test('the scan is pointed at the running session', () => {
  const args = performanceScanArgs('C:\\runtime', settings, { seconds: 10, version: '0.1.4', packageName: 'com.example.game' });
  assert.equal(value(args, '-File'), path.join('C:\\runtime', SCAN_SCRIPT));
  assert.equal(value(args, '-Sdk'), settings.sdk);
  assert.equal(value(args, '-DataHome'), path.join(settings.managedDirectory, 'output'));
  assert.equal(value(args, '-Port'), '5580');
  assert.equal(value(args, '-Seconds'), '10');
  assert.equal(value(args, '-Package'), 'com.example.game');
  assert.equal(value(args, '-Version'), '0.1.4');
});

test('what the launcher does not know is left for the script to work out', () => {
  // -File binds by presence, so an empty package would arrive as the literal
  // string "" and beat the script's own foreground detection.
  const args = performanceScanArgs('C:\\runtime', settings, {});
  assert.ok(!args.includes('-Package'));
  assert.ok(!args.includes('-Version'));
  assert.equal(value(args, '-Seconds'), '10');
});

test('an unset SDK is omitted rather than passed as "undefined"', () => {
  const args = performanceScanArgs('C:\\runtime', {}, {});
  assert.ok(!args.includes('-Sdk'));
  assert.ok(!args.includes('-Port'));
});

test('the scan window is bounded the same way the script bounds it', () => {
  for (const seconds of [2, 61, 10.5, '10', null]) {
    assert.throws(() => performanceScanArgs('C:\\runtime', settings, { seconds }), /between 3 and 60/);
  }
  assert.doesNotThrow(() => performanceScanArgs('C:\\runtime', settings, { seconds: 3 }));
  assert.doesNotThrow(() => performanceScanArgs('C:\\runtime', settings, { seconds: 60 }));
});

test('the timeout leaves room for the window itself plus slow ADB round trips', () => {
  assert.ok(performanceScanTimeout(10) > 10_000, 'a timeout inside the sample window would kill every scan');
  assert.ok(performanceScanTimeout(60) > performanceScanTimeout(10));
});

test('the script cleans up after itself and only reads', async () => {
  // A diagnostic that writes to the guest, or leaves a file behind, turns a
  // performance complaint into a second problem to explain.
  const script = await fs.readFile(path.join(root, SCAN_SCRIPT), 'utf8');
  for (const forbidden of [/\badb\b.*\bpush\b/i, /Remove-Item/, /\bsetprop\b/, /\bpm (install|uninstall)\b/]) {
    assert.doesNotMatch(script, forbidden, `the scan must not use ${forbidden}`);
  }
});
