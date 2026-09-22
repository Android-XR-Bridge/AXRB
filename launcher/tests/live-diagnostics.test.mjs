import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { LiveDiagnostics, transientAdbFault } from '../core/live-diagnostics.mjs';
import { collectDiagnostics } from '../core/diagnostics.mjs';
import { Runtime, run } from '../core/runtime.mjs';
import { Setup } from '../core/setup.mjs';

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

test('stream fragments are reassembled and oversized credentials retain surrounding context', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}) });
  capture.write('android', '09-21 03:04:05.678  123  456 E AndroidRuntime: access_to');
  assert.deepEqual(capture.snapshot().entries, []);
  capture.write('android', 'ken=secret-value\n');
  const entry = capture.snapshot().entries[0];
  assert.equal(entry.level, 'E'); assert.equal(entry.pid, '123'); assert.equal(entry.tag, 'AndroidRuntime');
  assert.equal(entry.guestTime, '09-21 03:04:05.678');
  assert.doesNotMatch(capture.text(), /secret-value/);
  capture.write('launcher', `request started access_token=${'x'.repeat(9000)}`);
  capture.write('launcher', 'SENSITIVE_REMAINDER status=ready\nnext safe event\n');
  assert.doesNotMatch(capture.text(), /SENSITIVE_REMAINDER|x{20}/);
  assert.match(capture.text(), /next safe event/);
  assert.match(capture.text(), /request started access_token=\[redacted\] status=ready/);
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

test('tail windows recover credential prefixes and keep the surrounding log message', async t => {
  const directory = await temporary(t), logs = path.join(directory, 'logs/game');
  await fs.mkdir(logs, { recursive: true });
  const file = path.join(logs, 'host.log');
  for (const limit of [64 * 1024, 128 * 1024]) {
    const line = 'request access_token=boundary-private-value status=ready\n';
    const offset = line.indexOf('boundary-private-value') + 5;
    await fs.writeFile(file, line + '\n'.repeat(limit - (line.length - offset)));
    let output;
    if (limit === 64 * 1024) {
      const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
      await capture.tailFiles(directory); await capture.tailFiles(directory);
      output = capture.text();
    } else output = await collectDiagnostics({ dataHome: directory });
    assert.doesNotMatch(output, /private-value/);
    assert.match(output, /request access_token=\[redacted\] status=ready/);
  }
});

test('rotating stderr cannot flush an unfinished stdout credential', async t => {
  const directory = await temporary(t), logs = path.join(directory, 'logs/emulator');
  await fs.mkdir(logs, { recursive: true });
  const stdout = path.join(logs, 'emulator.stdout.log'), stderr = path.join(logs, 'emulator.stderr.log');
  await fs.writeFile(stdout, 'boot request access_to');
  await fs.writeFile(stderr, 'previous warning\n');
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
  await capture.tailFiles(directory);
  await fs.writeFile(stderr, 'reset\n');
  await capture.tailFiles(directory);
  await fs.appendFile(stdout, 'ken=rotation-private-value status=ready\nuseful unfinished message');
  await capture.tailFiles(directory);
  await fs.writeFile(stdout, 'new boot\n');
  await capture.tailFiles(directory);
  assert.doesNotMatch(capture.text(), /rotation-private-value/);
  assert.match(capture.text(), /boot request access_token=\[redacted\] status=ready/);
  assert.match(capture.text(), /useful unfinished message/);
  assert.match(capture.text(), /reset/);
  assert.match(capture.text(), /new boot/);
});

test('long ordinary lines and unfinished messages are retained rather than omitted', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}) });
  const message = `driver details ${'frame=ready; '.repeat(1000)}last detail`;
  capture.write('host', message);
  capture.flush('host');
  assert.equal(capture.snapshot().entries.map(entry => entry.text).join(''), message);
});

test('setup process output redacts split credentials without mixing stderr or losing final fragments', async t => {
  const directory = await temporary(t);
  const stdoutSeen = path.join(directory, 'stdout-seen'), stderrSeen = path.join(directory, 'stderr-seen');
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
  const setup = new Setup({ root: directory, directory, runtime: { settings: {} }, changed() {},
    onOutput: (text, metadata) => capture.write('launcher', text, { tag: 'setup', ...metadata }) });
  const script = `
    const fs = require('node:fs/promises');
    const wait = async file => { while (!(await fs.access(file).then(() => true, () => false))) await new Promise(resolve => setTimeout(resolve, 10)); };
    (async () => {
      process.stdout.write('request access_to');
      await wait(${JSON.stringify(stdoutSeen)});
      process.stderr.write('stderr warning\\n');
      await wait(${JSON.stringify(stderrSeen)});
      process.stdout.write('ken=process-private-value status=ready\\nfinal partial message');
    })();
  `;
  await run(process.execPath, ['-e', script], { timeout: 5000, onOutput: (text, metadata) => {
    setup.appendLog(text, metadata);
    if (text.includes('request access_to')) fs.writeFile(stdoutSeen, '').catch(() => {});
    if (text.includes('stderr warning')) fs.writeFile(stderrSeen, '').catch(() => {});
  } });
  await until(() => capture.text().includes('final partial message'));
  assert.doesNotMatch(capture.text(), /process-private-value/);
  assert.match(capture.text(), /request access_token=\[redacted\] status=ready/);
  assert.match(capture.text(), /stderr warning/);
  assert.match(capture.text(), /final partial message/);
  const transcript = setup.status.logs.join('\n');
  const bundle = await collectDiagnostics({ dataHome: directory, setupLogs: setup.status.logs, liveLogs: capture.text() });
  assert.doesNotMatch(transcript + bundle, /process-private-value/);
  assert.match(transcript, /request access_token=\[redacted\] status=ready/);
  assert.match(transcript, /final partial message/);
});

test('runtime stderr preserves inferred info, warning and fatal severity', async () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}) });
  await run(process.execPath, ['-e', "process.stderr.write('startup complete\\nwarning: using fallback\\nfatal: renderer crashed\\n')"], {
    onOutput: (text, metadata) => capture.write('launcher', text, { tag: 'runtime', ...metadata }),
  });
  assert.deepEqual(capture.snapshot().entries.map(({ level, text }) => ({ level, text })), [
    { level: 'I', text: 'startup complete' },
    { level: 'W', text: 'warning: using fallback' },
    { level: 'F', text: 'fatal: renderer crashed' },
  ]);
});

test('game exit publishes final fragments while inherited pipes stay open without flushing another stream', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-game-diagnostics-'));
  const pidFile = path.join(directory, 'child.pid');
  let child;
  t.after(async () => {
    child?.kill();
    const pid = Number(await fs.readFile(pidFile, 'utf8').catch(() => '0'));
    if (pid) { try { process.kill(pid); } catch {} }
    await fs.rm(directory, { recursive: true, force: true, maxRetries: 30, retryDelay: 100 });
  });
  await fs.mkdir(path.join(directory, 'scripts/run'), { recursive: true });
  const literal = value => `'${value.replaceAll("'", "''")}'`;
  await fs.writeFile(path.join(directory, 'scripts/run/run_windows_game.ps1'), [
    'param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName)',
    `$out = ${literal(path.join(directory, 'child.out'))}`,
    `$err = ${literal(path.join(directory, 'child.err'))}`,
    "$p = Start-Process powershell.exe -ArgumentList '-NoProfile','-Command','Start-Sleep -Seconds 60' -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err",
    `[IO.File]::WriteAllText(${literal(pidFile)}, [string]$p.Id)`,
    '[Console]::Out.Write("final stdout fragment")',
    '[Console]::Error.Write("final stderr fragment")',
    'exit 7',
  ].join('\n'));
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
  const otherStream = { tag: 'runtime', stream: 'another-process:stdout' };
  capture.write('launcher', 'concurrent access_to', otherStream);
  const runtime = new Runtime(directory, { avd: 'test', port: 5580, sdk: directory, memoryMB: 8192, cpuCores: 4 },
    (text, metadata) => capture.write('launcher', text, metadata));
  const result = await new Promise(resolve => {
    runtime.launch({ id: 'test', package: 'com.example.game', activity: 'com.example.game/.Main', name: 'Test' }, (code, output) => {
      resolve({ code, output, entriesAtExit: capture.snapshot().entries });
    });
    child = runtime.child;
  });
  const pid = Number(await fs.readFile(pidFile, 'utf8'));
  process.kill(pid, 0); // The descendant must still be alive when diagnostics finish.
  assert.equal(result.code, 7);
  assert.deepEqual(result.entriesAtExit.map(entry => entry.text).sort(), ['final stderr fragment', 'final stdout fragment']);
  assert.match(result.output, /final stdout fragment/);
  assert.match(result.output, /final stderr fragment/);
  assert.equal(runtime.game, null);
  capture.write('launcher', 'ken=peer-private-value status=ready\n', otherStream);
  assert.doesNotMatch(capture.text(), /peer-private-value/);
  assert.match(capture.text(), /concurrent access_token=\[redacted\] status=ready/);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(capture.snapshot().entries.filter(entry => entry.text === 'final stdout fragment').length, 1);
  assert.equal(capture.snapshot().entries.filter(entry => entry.text === 'final stderr fragment').length, 1);
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

test('a server that is still starting is waited for and retried, not reported as a failure', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  await fs.writeFile(path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb'), '');
  const seen = [];
  let listings = 0;
  const spawnProcess = (_file, args) => {
    const child = new EventEmitter();
    child.stdout = new PassThrough(); child.stderr = new PassThrough();
    child.kill = () => { child.stdout.end(); child.stderr.end(); queueMicrotask(() => child.emit('close', null)); };
    seen.push(args.join(' '));
    queueMicrotask(() => {
      if (args.includes('devices')) {
        // The first client loses the race with the forking server, exactly as
        // a real one does; the second sees a healthy server.
        if (++listings === 1) {
          child.stderr.end("adb.exe: failed to check server version: protocol fault (couldn't read status): connection reset\n");
          child.emit('close', 1);
        } else {
          child.stdout.end('List of devices attached\nemulator-5584\tdevice model:test\n');
          child.emit('close', 0);
        }
      } else if (args.includes('start-server')) {
        child.stdout.end(''); child.emit('close', 0);
      } else if (args.includes('logcat')) {
        child.stdout.write('09-21 03:04:05.678  123  456 I test: recovered\n');
      } else { child.stdout.end('avd\nOK\n'); child.emit('close', 0); }
    });
    return child;
  };
  const capture = new LiveDiagnostics({ directory: path.join(directory, 'history'), getConfig: () => ({ sdk: directory, port: 5584, dataHome: directory }), spawnProcess });
  await capture.start();
  try {
    await until(() => capture.snapshot().android.state === 'streaming');
    assert.equal(capture.snapshot().android.serial, 'emulator-5584');
    assert.ok(seen.some(command => command.includes('start-server')), 'the server is started deliberately after a protocol fault');
    assert.doesNotMatch(capture.text(), /protocol fault[\s\S]*Diagnostics:/);
  } finally { await capture.stop(); }
});

test('a protocol fault is classified as transient while a real failure is not', () => {
  assert.equal(transientAdbFault("adb.exe: failed to check server version: protocol fault (couldn't read status): connection reset"), true);
  assert.equal(transientAdbFault('ADB device discovery timed out.'), true);
  assert.equal(transientAdbFault('cannot connect to daemon at tcp:5038'), true);
  assert.equal(transientAdbFault('More than one emulator runs same-avd; select its console port in runtime settings.'), false);
  assert.equal(transientAdbFault(''), false);
});

test('capture stops probing ADB while setup or an operation owns it', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  await fs.writeFile(path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb'), '');
  let busy = true;
  const device = simulatedAdb([{ serial: 'emulator-5584', name: 'axrb-managed-api36' }]);
  const seen = [];
  const spawnProcess = (file, args) => { seen.push(args.join(' ')); return device.spawnProcess(file, args); };
  const capture = new LiveDiagnostics({
    directory: path.join(directory, 'history'),
    getConfig: () => ({ sdk: directory, port: 5584, dataHome: directory }),
    spawnProcess, adbBusy: () => busy,
  });
  await capture.start();
  try {
    await new Promise(resolve => setTimeout(resolve, 300));
    assert.equal(seen.length, 0, 'no adb client is spawned while the gate is held');
    busy = false;
    await until(() => capture.snapshot().android.state === 'streaming');
    assert.ok(seen.some(command => command.includes('devices')), 'discovery resumes once the gate clears');
  } finally { await capture.stop(); }
});
