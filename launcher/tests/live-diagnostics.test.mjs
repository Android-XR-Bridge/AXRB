import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { LiveDiagnostics } from '../core/live-diagnostics.mjs';
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
        // The real emulator console ends each line with \r\r\n.
        } else finish(`${device.name}\r\r\nOK\r\r\n`);
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

test('emulator console floods evict boot chatter before launcher and failing guest lines', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 4, maxBytes: 64 * 1024 });
  capture.append('launcher', 'capture attached to emulator-5584', { tag: 'logcat' });
  capture.append('emulator', 'warning: guest compositor stalled');
  capture.append('emulator', 'binder_alloc: 1873: binder_alloc_buf size 2416648 failed, no address space');
  for (let index = 1; index <= 8; index++) capture.append('emulator', `[    ${index}.000000] init: starting service 'zygote'`);
  const texts = capture.snapshot().entries.map(entry => entry.text);
  assert.ok(texts.includes('capture attached to emulator-5584'));
  assert.ok(texts.includes('warning: guest compositor stalled'));
  assert.ok(texts.includes('binder_alloc: 1873: binder_alloc_buf size 2416648 failed, no address space'));
  assert.deepEqual(texts.filter(text => text.includes("service 'zygote'")), ["[    8.000000] init: starting service 'zygote'"]);
  assert.equal(capture.snapshot().dropped, 7);
});

test('android logcat floods evict routine lines before setup history, AXRB lines and failures', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 5, maxBytes: 64 * 1024 });
  capture.append('launcher', 'Setup: download · Android Emulator', { tag: 'setup' });
  capture.append('android', '09-22 23:43:31.628  812  812 E AndroidRuntime: FATAL EXCEPTION: main');
  capture.append('android', '09-22 23:43:31.700  901  930 I AXRB.Image: connected to local image proxy');
  for (let index = 0; index < 10; index++) capture.append('android', `09-22 23:43:32.${100 + index}  700  700 D PstnIncomingCallNotifier: Registering: [${index}]`);
  const texts = capture.snapshot().entries.map(entry => entry.text);
  assert.ok(texts.includes('Setup: download · Android Emulator'));
  assert.ok(texts.includes('FATAL EXCEPTION: main'));
  assert.ok(texts.includes('connected to local image proxy'));
  assert.deepEqual(texts.filter(text => text.startsWith('Registering')), ['Registering: [8]', 'Registering: [9]']);
});

test("Android apps' warning floods evict before launcher lines and crashes", () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 4, maxBytes: 64 * 1024 });
  capture.append('launcher', 'login completed (50774 ms)', { tag: 'operation' });
  capture.append('android', '09-23 00:27:16.000  812  812 E AndroidRuntime: FATAL EXCEPTION: main');
  for (let index = 0; index < 6; index++) capture.append('android', `09-23 00:27:16.${100 + index}  900  930 W PlatformConfigurator: \tat fyac.run(${index})`);
  const texts = capture.snapshot().entries.map(entry => entry.text);
  assert.deepEqual(texts, ['login completed (50774 ms)', 'FATAL EXCEPTION: main', '\tat fyac.run(4)', '\tat fyac.run(5)']);
});

test('a third-party android tag over its budget is summarized, not stored line by line', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 1000, maxBytes: 4 * 1024 * 1024 });
  const at = ms => new Date(Date.parse('2026-09-23T00:27:16.000Z') + ms).toISOString();
  for (let index = 0; index < 250; index++) capture.append('android', `09-23 00:27:16.000  900  930 W PlatformConfigurator: \tat fyac.run(${index})`, { receivedAt: at(index) });
  for (let index = 0; index < 150; index++) capture.append('android', `09-23 00:27:16.000  901  931 I AXRB.Image: frame ${index}`, { receivedAt: at(index) });
  capture.append('android', '09-23 00:27:16.000  812  812 E AndroidRuntime: FATAL EXCEPTION: main', { receivedAt: at(300) });
  const tagged = tag => capture.snapshot().entries.filter(entry => entry.tag === tag);
  assert.equal(tagged('PlatformConfigurator').length, 100);
  assert.equal(tagged('AXRB.Image').length, 150, "AXRB's own lines are never limited");
  assert.equal(tagged('AndroidRuntime').length, 1);
  capture.append('android', '09-23 00:27:27.000  900  930 W PlatformConfigurator: next window', { receivedAt: at(11000) });
  assert.deepEqual(tagged('PlatformConfigurator').slice(-2).map(entry => [entry.level, entry.text]),
    [['W', '[150 more PlatformConfigurator lines suppressed within 10s]'], ['W', 'next window']]);
  for (let index = 0; index < 105; index++) capture.append('android', `09-23 00:27:27.000  900  930 W PlatformConfigurator: again ${index}`, { receivedAt: at(11001) });
  capture.flushSuppressed(true);
  assert.equal(tagged('PlatformConfigurator').at(-1).text, '[6 more PlatformConfigurator lines suppressed within 10s]', 'a tag that falls silent still reports its count');
});

test('kernel boot chatter is demoted to debug while real guest failures keep severity', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 64, maxBytes: 64 * 1024 });
  capture.append('emulator', '[    9.412037] ueventd: firmware_load: error -2 opening file');
  capture.append('emulator', "init: warning: could not parse /vendor/etc/public.libraries.txt");
  capture.append('emulator', '[   88.290614] cfg80211: failed to load regulatory.db');
  capture.append('emulator', "[    7.367971] init: Command 'setprop debug.stagefright.ccodec' failed: property doesn't exist");
  capture.append('emulator', '[    0.365469] ACPI: _OSC evaluation for CPUs failed, trying _PDC');
  capture.append('emulator', '[   11.676575] platform regulatory.0: Direct firmware load for regulatory.db failed with error -2');
  capture.append('emulator', '[    0.039569] Speculative Return Stack Overflow: WARNING: See https://kernel.org/doc/html/latest/admin-guide/hw-vuln/srso.html for mitigation options.');
  capture.append('emulator', "[   98.282931] capability: warning: `wpa_supplicant' uses 32-bit capabilities (legacy support in use)");
  capture.append('emulator', '[  101.553416] audio: error - unable to handle stream (call trace dumped)');
  capture.append('emulator', 'binder: 1873:1873 transaction failed 29189/-3, size 0-0 line 3134');
  capture.append('emulator', '[   38.869677] binder_alloc: 2674: binder_alloc_buf size 1056768 failed, no address space');
  capture.append('emulator', '[   12.553416] Kernel panic - not syncing: attempted to kill init');
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.level), ['D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'E', 'E', 'E', 'F']);
});

test("the emulator's own severity label outranks keywords in its message", () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 16, maxBytes: 64 * 1024 });
  capture.append('emulator', 'WARNING      | Failed to load snapshot \'default_boot\'');
  capture.append('emulator', 'INFO         | Checking: hasSufficientHwGpu error budget');
  capture.append('emulator', 'ERROR        | Unable to connect to adb daemon');
  capture.append('emulator', 'FATAL        | Cannot start AVD');
  capture.append('emulator', 'input_len: 0x13e1WARNING      |3 Failed to process .ini file');
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.level), ['W', 'I', 'E', 'F', 'E']);
});

test('the boot wait heartbeat is informational until the emulator process dies', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 16, maxBytes: 64 * 1024 });
  capture.append('launcher', 'Android startup diagnostic: 31s elapsed; adb=offline (error: device offline (exit 1)); boot=(getprop failed: adb.exe: device offline (exit 1)); processExited=False');
  capture.append('launcher', 'Android startup diagnostic: 62s elapsed; adb=unreachable: timed out; boot=; processExited=True');
  capture.append('launcher', 'Android startup diagnostic: warning: restarting ADB server after persistent offline transport.');
  capture.append('launcher', 'Android startup diagnostic: SDK failed to verify');
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.level), ['I', 'E', 'W', 'E']);
});

test('host loader lines reporting Windows error 0 stay informational', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 16, maxBytes: 64 * 1024 });
  capture.append('host', 'OpenXR.dll loaded (Windows error 0)');
  capture.append('host', 'controller state remapped (Windows error 0)');
  capture.append('host', 'driver init error before retry (Windows error 0) unresolved');
  capture.append('host', 'device failed to load (Windows error 126)');
  assert.deepEqual(capture.snapshot().entries.map(entry => entry.level), ['I', 'I', 'E', 'E']);
});

test('discovery misses log the raw device list informationally, once per distinct listing', async t => {
  const directory = await temporary(t);
  const devices = [{ serial: 'emulator-5678', name: 'other-avd' }];
  const adb = simulatedAdb(devices);
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}), spawnProcess: adb.spawnProcess });
  capture.active = true;
  const discover = () => capture.discover('adb', { port: 5584, avd: 'wanted-avd' }, 0);
  const reports = () => capture.snapshot().entries.filter(entry => entry.source === 'launcher' && /^No matching Android device/.test(entry.text));
  assert.equal(await discover(), null);
  assert.deepEqual(reports().map(entry => entry.level), ['I'], 'no emulator yet is the normal waiting state');
  assert.match(capture.text(), /emulator-5678\s+device model:test/);
  assert.match(capture.text(), /emulator-5678 reports AVD name "other-avd"/, 'a connected emulator explains why it was not captured');
  for (let attempt = 0; attempt < 20; attempt++) await discover();
  assert.equal(reports().length, 1, 'an idle launcher does not repeat an unchanged listing');
  devices.push({ serial: 'emulator-5690', name: 'third-avd' });
  await discover();
  assert.equal(reports().length, 2, 'a changed listing is reported');
  devices.length = 0; devices.push({ serial: 'emulator-5584', name: 'wanted-avd' });
  assert.equal(await discover(), 'emulator-5584');
  devices.length = 0; devices.push({ serial: 'emulator-5678', name: 'other-avd' });
  await discover();
  assert.equal(reports().length, 3, 'a new waiting episode reports its first listing again');
  devices.length = 0;
  await discover();
  assert.equal(reports().at(-1).text, 'No matching Android device; adb reports no devices attached.');
});

test('a missing adb before setup installs the SDK is a waiting state, not an error', async t => {
  const directory = await temporary(t);
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
  capture.active = true;
  await capture.connect({ sdk: path.join(directory, 'not-installed'), port: 5584 }, capture.generation);
  assert.equal(capture.snapshot().android.state, 'waiting');
  assert.deepEqual(capture.snapshot().entries.map(entry => [entry.level, entry.text]),
    [['I', 'Android: ADB is not installed at the configured SDK path. Capture will start when it is available.']]);
});

test('setup transcript lines name the component each phase works on', async t => {
  const directory = await temporary(t);
  const lines = [];
  const setup = new Setup({ root: directory, directory, runtime: { settings: {} }, changed() {}, onOutput: text => lines.push(text.trim()) });
  setup.update({ phase: 'verify', component: '' });
  setup.update({ phase: 'verify', component: 'Android Emulator', completed: 0, total: 10 });
  setup.update({ phase: 'download' });
  setup.update({ completed: 5, total: 10 });
  setup.update({ phase: 'extract', completed: 0, total: 0 });
  setup.update({ phase: 'boot', component: 'Starting Android' });
  setup.update({ component: 'Installing AXRB runtime' });
  setup.update({ phase: 'ready', component: '' });
  assert.deepEqual(lines, ['Setup: verify', 'Setup: verify · Android Emulator', 'Setup: download · Android Emulator', 'Setup: extract · Android Emulator',
    'Setup: boot · Starting Android', 'Setup: boot · Installing AXRB runtime', 'Setup: ready']);
});

test('each logcat attachment records the exact adb argv as a launcher line', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  const executable = path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb');
  await fs.writeFile(executable, '');
  const adb = simulatedAdb([{ serial: 'emulator-5584', name: 'test-avd' }]);
  const capture = new LiveDiagnostics({ directory: path.join(directory, 'history'), getConfig: () => ({ sdk: directory, port: 5584, avd: 'test-avd', dataHome: directory }), spawnProcess: adb.spawnProcess });
  await capture.start();
  try {
    await until(() => capture.snapshot().android.state === 'streaming');
    assert.deepEqual(capture.snapshot().entries.filter(entry => /logcat -b main/.test(entry.text))
      .map(entry => [entry.source, entry.level, entry.text]),
    [['launcher', 'I', `${path.basename(executable)} -P 5038 -s emulator-5584 logcat -b main -b system -b crash -v threadtime -T 200`]]);
  } finally { await capture.stop(); }
  assert.equal(adb.children.size, 0);
});

test('a logcat that keeps failing before any output reports one episode, not every retry', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  const executable = path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb');
  await fs.writeFile(executable, '');
  let output = '';
  const spawnProcess = (_file, args) => {
    const child = new EventEmitter();
    child.stdout = new PassThrough(); child.stderr = new PassThrough(); child.kill = () => {};
    const logcat = args.includes('logcat');
    queueMicrotask(() => {
      if (args.includes('devices')) child.stdout.end('List of devices attached\nemulator-5584\tdevice model:test\n');
      else if (output) child.stdout.write(output);
      else child.stderr.write('error: device offline\n');
      child.stdout.end(); child.stderr.end();
      setTimeout(() => child.emit('close', logcat && !output ? 1 : 0), 5);
    });
    return child;
  };
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}), spawnProcess });
  capture.active = true;
  const config = { sdk: directory, port: 5584 };
  const logcatLines = () => capture.snapshot().entries.filter(entry => entry.tag === 'logcat' && !entry.text.startsWith('List of devices'))
    .map(entry => [entry.level, entry.text.replace(/^\S*adb(?:\.exe)? .*/, 'argv')]);
  const attach = async () => { await capture.connect(config, capture.generation); await until(() => !capture.stream); };
  for (let attempt = 0; attempt < 3; attempt++) await attach();
  assert.deepEqual(logcatLines(), [
    ['I', 'emulator-5584: ADB is online; waiting for logcat output.'],
    ['I', 'argv'],
    ['W', 'error: device offline'],
    ['E', 'emulator-5584: error: device offline'],
  ]);
  assert.equal(capture.snapshot().android.state, 'error', 'the panel still follows every retry');
  assert.match(capture.text(), /^Android capture: error — error: device offline \(repeated 2 more times\)\n/);
  output = '09-21 03:04:05.678  123  456 I test: hello\n';
  await attach();
  assert.deepEqual(logcatLines().slice(4), [
    ['W', 'Logcat failure repeated 2 more times: error: device offline'],
    ['I', 'emulator-5584: Receiving Android logs.'],
    ['W', 'emulator-5584: Logcat disconnected; waiting to reconnect.'],
  ], 'recovery reports the suppressed count and closes the episode');
  assert.doesNotMatch(capture.text().split('\n')[0], /repeated \d+ more time/);
  output = '';
  await attach();
  assert.deepEqual(logcatLines().slice(7).map(([level]) => level), ['I', 'I', 'W', 'E'], 'a failure after recovery is a new episode, reported in full');
});

test('a streaming logcat that ends because Android shut down is a disconnect, not an error', async t => {
  const directory = await temporary(t);
  await fs.mkdir(path.join(directory, 'platform-tools'));
  await fs.writeFile(path.join(directory, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb'), '');
  const spawnProcess = (_file, args) => {
    const child = new EventEmitter();
    child.stdout = new PassThrough(); child.stderr = new PassThrough(); child.kill = () => {};
    queueMicrotask(() => {
      if (args.includes('devices')) child.stdout.end('List of devices attached\nemulator-5584\tdevice model:test\n');
      else { child.stdout.write('09-22 23:43:40.000  123  456 I test: shutting down\n'); child.stderr.write('error: device offline\n'); }
      child.stdout.end(); child.stderr.end();
      setTimeout(() => child.emit('close', args.includes('logcat') ? 1 : 0), 5);
    });
    return child;
  };
  const capture = new LiveDiagnostics({ directory, getConfig: () => ({}), spawnProcess });
  capture.active = true;
  await capture.connect({ sdk: directory, port: 5584 }, capture.generation);
  await until(() => !capture.stream);
  assert.equal(capture.snapshot().android.state, 'disconnected');
  assert.deepEqual(capture.snapshot().entries.filter(entry => entry.text.startsWith('emulator-5584: error'))
    .map(entry => entry.level), ['W']);
});

test('perf counters keep bounded windows, render one line each and warn on sustained slow frames', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 100, maxBytes: 1024 * 1024 });
  const at = seconds => new Date(Date.parse('2026-09-22T10:00:00Z') + seconds * 1000).toISOString();
  const record = (counter, rate, p95, seconds) =>
    capture.append('host', `AXRB.Perf ${counter}: rate=${rate}/s avg=0.4ms samples=120 p50=0.200ms p95=${p95}ms p99=0.610ms`, { receivedAt: at(seconds) });
  assert.equal(capture.perfText(), '');
  for (let seconds = 0; seconds < 14; seconds++) record('host-end-frame', '75', '120', seconds);
  record('host-frame-submit', '150.5', '90', 14);
  capture.append('android', '09-22 10:00:15.000  123  456 I AXRB.Perf: AXRB.Perf end-frame: rate=75/s avg=0.4ms samples=120 p50=0.200ms p95=120ms p99=0.610ms', { receivedAt: at(15) });
  assert.equal(capture.perfText(), [
    'host-end-frame: rate=75.0/s p50=0.200ms p95=120.000ms p99=0.610ms (12 windows)',
    'host-frame-submit: rate=150.5/s p50=0.200ms p95=90.000ms p99=0.610ms (1 window)',
    'end-frame: rate=75.0/s p50=0.200ms p95=120.000ms p99=0.610ms (1 window)',
  ].join('\n'));
  assert.equal(capture.snapshot().entries.filter(entry => entry.tag === 'perf').length, 0);
  record('host-selected-frame-age', '75', '120', 20);
  record('host-selected-frame-age', '75', '260', 50);
  assert.equal(capture.snapshot().entries.filter(entry => entry.tag === 'perf').length, 0);
  record('host-selected-frame-age', '75', '280', 80);
  record('host-selected-frame-age', '75', '300', 100);
  record('host-selected-frame-age', '75', '290', 145);
  assert.deepEqual(capture.snapshot().entries.filter(entry => entry.tag === 'perf')
    .map(entry => [entry.source, entry.level, entry.text]), [
    ['launcher', 'W', 'frame pipeline degraded: host-selected-frame-age p95=280ms sustained'],
    ['launcher', 'W', 'frame pipeline degraded: host-selected-frame-age p95=290ms sustained'],
  ]);
  assert.equal(capture.perfText().split('\n')[3], 'host-selected-frame-age: rate=75.0/s p50=0.200ms p95=290.000ms p99=0.610ms (5 windows)');
  capture.resetPerf();
  assert.equal(capture.perfText(), '', 'a new game session starts without the previous windows');
  record('host-selected-frame-age', '75', '300', 150);
  record('host-selected-frame-age', '75', '300', 151);
  assert.equal(capture.snapshot().entries.filter(entry => entry.tag === 'perf').length, 3, 'the warning interval restarts with the session');
});

test('replayed history keeps its perf warning without repeating it or reviving old windows', async t => {
  const directory = await temporary(t);
  const at = seconds => new Date(Date.parse('2026-09-22T10:00:00Z') + seconds * 1000).toISOString();
  const warnings = capture => capture.snapshot().entries.filter(entry => entry.tag === 'perf').length;
  for (let run = 1; run <= 3; run++) {
    const capture = new LiveDiagnostics({ directory, getConfig: () => ({}) });
    capture.tick = async () => {};
    await capture.start();
    if (run === 1) for (const seconds of [0, 10]) capture.append('host', 'AXRB.Perf host-end-frame: rate=75/s avg=1ms p50=1ms p95=300ms p99=400ms', { receivedAt: at(seconds) });
    assert.equal(warnings(capture), 1, `launch ${run}`);
    if (run > 1) assert.equal(capture.perfText(), '');
    await capture.stop();
  }
});

test('readers learn which retained entries were evicted from the middle', () => {
  const capture = new LiveDiagnostics({ directory: '.', getConfig: () => ({}), maxEntries: 3, maxBytes: 64 * 1024 });
  capture.append('launcher', 'kept');
  capture.append('emulator', 'chatter 1');
  capture.append('emulator', 'chatter 2');
  const first = capture.snapshot(0, -1);
  assert.equal(first.reset, true);
  assert.deepEqual(first.entries.map(entry => entry.text), ['kept', 'chatter 1', 'chatter 2']);
  capture.append('emulator', 'chatter 3');
  const next = capture.snapshot(first.lastId, first.evictionSeq);
  assert.equal(next.reset, false);
  assert.deepEqual(next.evicted, [first.entries[1].id]);
  assert.deepEqual(next.entries.map(entry => entry.text), ['chatter 3']);
  assert.deepEqual(capture.snapshot(next.lastId, next.evictionSeq).evicted, []);
  assert.equal(capture.snapshot(next.lastId, next.evictionSeq - 99).reset, true, 'a cursor older than the log resets');
});
