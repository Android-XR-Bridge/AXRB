import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { LiveDiagnostics } from '../core/live-diagnostics.mjs';
import { collectDiagnostics } from '../core/diagnostics.mjs';

async function temporary(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-diagnostics-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  return directory;
}
async function until(predicate) {
  const deadline = Date.now() + 5000;
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error('Diagnostic transition did not complete.');
    await new Promise(resolve => setTimeout(resolve, 10));
  }
}
function simulatedAdb(devices) {
  const streams = new Map(), children = new Set();
  const spawnProcess = (_file, args) => {
    const child = new EventEmitter();
    child.stdout = new PassThrough(); child.stderr = new PassThrough();
    children.add(child);
    child.kill = () => { if (!children.delete(child)) return; child.stdout.end(); child.stderr.end(); queueMicrotask(() => child.emit('close', null)); };
    const finish = text => { child.stdout.end(text); children.delete(child); child.emit('close', 0); };
    queueMicrotask(() => {
      if (!children.has(child)) return;
      if (args.includes('devices')) finish(`List of devices attached\n${devices.map(device => `${device.serial}\tdevice model:test`).join('\n')}\n`);
      else {
        const serial = args[args.indexOf('-s') + 1];
        const device = devices.find(value => value.serial === serial);
        if (args.includes('logcat')) {
          streams.set(serial, child);
          child.stdout.write(`09-21 03:04:05.678  123  456 I ${device.name}: stream for ${device.name}\n`);
        } else finish(`${device.name}\nOK\n`);
      }
    });
    return child;
  };
  return { spawnProcess, streams, children };
}

test('stream fragments stay private until complete and oversized secrets are discarded', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}) });
  capture.write('android', '09-21 03:04:05.678  123  456 E AndroidRuntime: access_to');
  assert.deepEqual(capture.snapshot().entries, []);
  capture.write('android', 'ken=secret-value\n');
  const entry = capture.snapshot().entries[0];
  assert.equal(entry.level, 'E'); assert.equal(entry.pid, '123'); assert.equal(entry.tag, 'AndroidRuntime');
  assert.equal(entry.guestTime, '09-21 03:04:05.678');
  assert.doesNotMatch(capture.text(), /secret-value/);
  capture.write('launcher', `access_token=${'x'.repeat(9000)}`);
  capture.write('launcher', 'SENSITIVE_REMAINDER\nnext safe event\n');
  assert.doesNotMatch(capture.text(), /SENSITIVE_REMAINDER|x{20}/);
  assert.match(capture.text(), /next safe event/);
});

test('bounded capture keeps newest events with monotonic cursors and reports discarded context', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 3, maxBytes: 1024 });
  for (let index = 1; index <= 6; index++) capture.append('host', `event ${index}`);
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.text), ['event 4', 'event 5', 'event 6']);
  assert.equal(capture.snapshot().dropped, 3);
  assert.deepEqual(capture.snapshot(5).entries.map(entry => entry.text), ['event 6']);
  capture.append('host', 'x'.repeat(2000));
  capture.append('host', 'last failure', { level: 'E' });
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.text), ['last failure']);
  assert.ok(capture.snapshot().lastId > 6);
});

test('failed-session history survives restart and appears redacted in existing export', async t => {
  const directory = await temporary(t), config = { sdk: path.join(directory, 'missing-sdk'), dataHome: directory, avd: 'machine-specific-name', port: 5678 };
  const first = new LiveDiagnostics({ directory, getConfig: () => config });
  await first.start();
  first.append('android', 'Game crashed; Authorization: Bearer sensitive-token', { level: 'F' });
  await first.stop();
  const saved = await fs.readFile(path.join(directory, 'recent.json'), 'utf8');
  assert.doesNotMatch(saved, /sensitive-token/);
  const second = new LiveDiagnostics({ directory, getConfig: () => config });
  await second.start();
  try {
    const bundle = await collectDiagnostics({ dataHome: directory, liveLogs: second.text() });
    assert.match(bundle, /Game crashed/); assert.doesNotMatch(bundle, /sensitive-token/);
    assert.match(bundle, /machine-specific-name/);
  } finally { await second.stop(); }
});

test('file capture follows appends and truncation without repeating earlier content', async t => {
  const directory = await temporary(t), logs = path.join(directory, 'logs/game');
  await fs.mkdir(logs, { recursive: true });
  const file = path.join(logs, 'host.log');
  await fs.writeFile(file, 'first event\n');
  const capture = new LiveDiagnostics({ directory: path.join(directory, 'history'), getConfig: () => ({ sdk: directory, dataHome: directory }) });
  await capture.tailFiles(directory);
  await fs.appendFile(file, 'second event\n');
  await capture.tailFiles(directory); await capture.tailFiles(directory);
  await fs.writeFile(file, 'new\n'); await capture.tailFiles(directory);
  assert.deepEqual(capture.snapshot().entries.filter(entry => entry.source === 'host').map(entry => entry.text), ['first event', 'second event', 'new']);
});

test('AVD discovery follows names across machine-specific ports and retires the old stream on changes', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  await fs.writeFile(path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb'), '');
  const device = simulatedAdb([{ serial: 'emulator-5678', name: 'charlie-test-avd' }, { serial: 'emulator-5584', name: 'other-avd' }, { serial: 'quest-usb', name: 'physical-headset' }]);
  let config = { sdk: directory, port: 5584, avd: 'charlie-test-avd', dataHome: directory };
  const capture = new LiveDiagnostics({ directory: path.join(directory, 'history'), getConfig: () => config, spawnProcess: device.spawnProcess });
  await capture.start();
  try {
    await until(() => capture.snapshot().android.state === 'streaming');
    assert.equal(capture.snapshot().android.serial, 'emulator-5678');
    assert.match(capture.text(), /stream for charlie-test-avd/);
    assert.doesNotMatch(capture.text(), /stream for other-avd|stream for physical-headset/);
    const old = device.streams.get('emulator-5678');
    config = { ...config, avd: 'other-avd' };
    await until(() => capture.snapshot().android.serial === 'emulator-5584' && capture.snapshot().android.state === 'streaming');
    assert.equal(old.stdout.writableEnded, true);
    assert.match(capture.text(), /stream for other-avd/);
  } finally { await capture.stop(); }
  assert.equal(device.children.size, 0);
});

test('ambiguous AVD names never attach to an arbitrary emulator', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  await fs.writeFile(path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb'), '');
  const device = simulatedAdb([{ serial: 'emulator-5560', name: 'same-avd' }, { serial: 'emulator-5678', name: 'same-avd' }]);
  const capture = new LiveDiagnostics({ directory: path.join(directory, 'history'), getConfig: () => ({ sdk: directory, port: 5584, avd: 'same-avd', dataHome: directory }), spawnProcess: device.spawnProcess });
  await capture.start();
  try {
    await until(() => capture.snapshot().android.state === 'error');
    assert.match(capture.snapshot().android.detail, /More than one emulator/);
    assert.equal(device.streams.size, 0);
  } finally { await capture.stop(); }
});
