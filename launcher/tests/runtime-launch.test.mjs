import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Runtime, run, openWindowsFeatures, windowsFeaturesCommand } from '../core/runtime.mjs';

test('Windows Features uses shell activation instead of spawning optionalfeatures directly', async () => {
  const args = windowsFeaturesCommand('C:\\root', 'C:\\Windows');
  assert.equal(args[0], '-NoProfile');
  assert.ok(!args.includes('-EncodedCommand'));
  assert.equal(args.at(-4), '-File');
  assert.match(args.at(-3), /open_windows_features\.ps1$/);
  assert.deepEqual(args.slice(-2), ['-WindowsDir', 'C:\\Windows']);
  // The activation itself moved into the script the launcher now runs by path.
  const script = await fs.readFile(new URL('../../scripts/run/open_windows_features.ps1', import.meta.url), 'utf8');
  assert.match(script, /Start-Process -LiteralPath/);
  assert.match(script, /System32[\\/]optionalfeatures\.exe/i);
  const calls = [];
  await openWindowsFeatures('C:\\root', async (...received) => { calls.push(received); return 'started'; });
  assert.equal(calls.length, 1);
  assert.equal(calls[0][0], 'powershell.exe');
  assert.ok(calls[0][1].includes('-File'));
});

test('Windows game launch actually executes PowerShell and reports its exit', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-launch-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'scripts/run'), { recursive: true });
  await fs.writeFile(path.join(root, 'scripts/run/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName)
Write-Output "EXECUTED:$Package CORES:$CpuCores"
exit 7
`);
  const runtime = new Runtime(root, { avd:'test',port:5580,sdk:root,memoryMB:8192,cpuCores:6 });
  const result = await new Promise(resolve => {
    runtime.launch({ id:'local:com.example.game',package:'com.example.game',activity:'com.example.game/.Main',name:'Test' }, (code, output) => resolve({code,output}));
    t.after(() => runtime.child?.kill());
  });
  assert.equal(result.code, 7); // -File propagates the script's own exit code.
  assert.match(result.output, /EXECUTED:com.example.game/);
  assert.match(result.output, /CORES:6/);
  assert.equal(runtime.game, null);
});

test('FPS HUD can be switched live through the session event', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-hud-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'scripts/run'), { recursive: true });
  await fs.copyFile(new URL('../../scripts/run/fps_hud.ps1', import.meta.url), path.join(root, 'scripts/run/fps_hud.ps1'));
  await fs.writeFile(path.join(root, 'scripts/run/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $CpuCores, $Package, $Activity, $GameName, [switch]$FpsHud, $FpsHudEventName)
$eventHandle = [System.Threading.EventWaitHandle]::new([bool]$FpsHud, [System.Threading.EventResetMode]::ManualReset, $FpsHudEventName)
try {
  if ($eventHandle.WaitOne(0)) { throw 'Expected HUD off initially' }
  if (!$eventHandle.WaitOne(5000)) { throw 'HUD never enabled' }
  Write-Output 'HUD_ON'
  $deadline = [DateTime]::UtcNow.AddSeconds(5)
  while ($eventHandle.WaitOne(0)) { if ([DateTime]::UtcNow -gt $deadline) { throw 'HUD never disabled' }; Start-Sleep -Milliseconds 10 }
  Write-Output 'HUD_OFF'
} finally { $eventHandle.Dispose() }
`);
  const runtime = new Runtime(root, { avd: 'test', port: 5580, sdk: root, memoryMB: 8192, fpsHud: false });
  let enabled;
  const on = new Promise(resolve => { enabled = resolve; });
  const ended = new Promise(resolve => {
    runtime.launch({ id: 'test', package: 'com.example.game', activity: 'com.example.game/.Main', name: 'Test' }, (code, output) => resolve({ code, output }));
    runtime.child.stdout.on('data', data => { if (data.toString().includes('HUD_ON')) enabled(); });
    t.after(() => runtime.child?.kill());
  });
  await runtime.setFpsHud(true); await on;
  await runtime.setFpsHud(false);
  const result = await ended;
  assert.equal(result.code, 0, result.output);
  assert.match(result.output, /HUD_OFF/);
  assert.equal(runtime.settings.fpsHud, false);
  await assert.rejects(runtime.setFpsHud('true'), /Invalid/);
});

test('run resolves when a long-lived grandchild keeps the stdio pipes open', { skip: process.platform !== 'win32', timeout: 20000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-pipes-'));
  let grandchild = 0;
  // The grandchild holds its redirected log files until it is gone, so stop it
  // before removing the directory and let Windows release the handles.
  t.after(async () => {
    try { process.kill(grandchild); } catch {}
    await fs.rm(root, { recursive: true, force: true, maxRetries: 30, retryDelay: 100 });
  });
  // The Android emulator inherits the launching script's pipes and outlives it,
  // so 'close' never fires. Settling on that event stalled setup until timeout.
  const script = path.join(root, 'spawner.ps1');
  await fs.writeFile(script, [
    `$out = '${path.join(root, 'child.out')}'`,
    `$err = '${path.join(root, 'child.err')}'`,
    "$p = Start-Process powershell.exe -ArgumentList '-NoProfile','-Command','Start-Sleep -Seconds 60' -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err",
    'Write-Output "Ready: $($p.Id)"',
  ].join('\n'));
  const started = Date.now();
  const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script], { timeout: 15000 });
  assert.match(output, /^Ready: \d+/m, 'the script transcript must survive the early settle');
  assert.ok(Date.now() - started < 10000, 'run must not wait for the grandchild to exit');
  grandchild = Number(output.match(/^Ready: (\d+)/m)[1]);
});

test('run still reports a failing exit code and its stderr', { skip: process.platform !== 'win32', timeout: 15000 }, async () => {
  await assert.rejects(
    run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', '[Console]::Error.WriteLine("boom"); exit 3']),
    /boom/);
});
