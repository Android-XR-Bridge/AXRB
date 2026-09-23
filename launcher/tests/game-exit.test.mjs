import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { run } from '../core/runtime.mjs';

const script = path.join(path.dirname(fileURLToPath(import.meta.url)), '../../scripts/run/run_windows_game.ps1');

// ApplicationExitInfo.dump() as Android prints it; reason names come from
// reasonCodeToString, and two of them contain brackets of their own.
const record = (timestamp, pid, reason, name, status, subreason = '0 (UNKNOWN)') => [
  `        ApplicationExitInfo #0:`,
  `          timestamp=${timestamp} pid=${pid} realUid=10218 packageUid=10218 definingUid=10218 user=0`,
  `          process=com.example.game reason=${reason} (${name}) subreason=${subreason} status=${status}`,
  `          importance=100 pss=0.00 rss=1.9GB description=null state=empty trace=null`,
].join('\n');
const dump = (...records) => ['ACTIVITY MANAGER PROCESS EXIT INFO (dumpsys activity exit-info)',
  '  package: com.example.game', '    Historical Process Exit for uid=10218', ...records].join('\n');

// Runs the script's own Get-GameExit with adb replaced by canned answers.
async function gameExit(t, { exitInfo, dmesg = '', pid = '4242', known = [] }) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-game-exit-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  await fs.writeFile(path.join(directory, 'exit-info.txt'), exitInfo);
  await fs.writeFile(path.join(directory, 'dmesg.txt'), dmesg);
  const harness = path.join(directory, 'harness.ps1');
  await fs.writeFile(harness, [
    "$ErrorActionPreference = 'Stop'",
    "$Package = 'com.example.game'",
    `$fixtures = '${directory.replaceAll("'", "''")}'`,
    'function Start-Sleep { }',
    'function Invoke-Adb([string[]]$Arguments, [int]$TimeoutMs = 10000) {',
    "    if ($Arguments -contains 'dmesg') { $text = [IO.File]::ReadAllText(\"$fixtures\\dmesg.txt\"); return @{ Code = $(if ($text) { 0 } else { 1 }); Text = $text; Error = '' } }",
    "    return @{ Code = 0; Text = [IO.File]::ReadAllText(\"$fixtures\\exit-info.txt\"); Error = '' }",
    '}',
    "$source = [IO.File]::ReadAllText($args[0])",
    "foreach ($name in 'Get-ExitRecords', 'Get-GameExit') { Invoke-Expression ([regex]::Match($source, \"(?s)function $name\\b.*?\\r?\\n}\\r?\\n\").Value) }",
    "$known = @($args[2] -split ',' | Where-Object { $_ })",
    'Get-GameExit $args[1] $known | ConvertTo-Json -Compress',
  ].join('\r\n'));
  const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', harness, script, pid, known.join(',')], { timeout: 30000 });
  return JSON.parse(output.trim());
}

test('a game that quits by itself reads as a quit, not a crash', { skip: process.platform !== 'win32' }, async t => {
  assert.deepEqual(await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 1, 'EXIT_SELF', 0)) }),
    { kind: 'exited', reason: 'EXIT_SELF', status: 0 });
  assert.equal((await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 2, 'SIGNALED', 9)) })).kind, 'exited',
    "a bare SIGKILL is the game's own quit");
});

test('crash reasons whose names contain brackets are still recognised', { skip: process.platform !== 'win32' }, async t => {
  assert.deepEqual(await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 5, 'APP CRASH(NATIVE)', 11)) }),
    { kind: 'crashed', reason: 'APP CRASH(NATIVE)', status: 11 });
  assert.equal((await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 4, 'APP CRASH(EXCEPTION)', 0)) })).reason, 'APP CRASH(EXCEPTION)');
  assert.deepEqual(await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 10, 'USER REQUESTED', 0, '21 (FORCE STOP)')) }),
    { kind: 'stopped', reason: 'USER REQUESTED / FORCE STOP', status: 0 });
});

test('a failed exit or a kernel out-of-memory kill is not a quit', { skip: process.platform !== 'win32' }, async t => {
  assert.deepEqual(await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 1, 'EXIT_SELF', 1)) }),
    { kind: 'failed', reason: 'EXIT_SELF, exit code 1', status: 1 });
  assert.deepEqual(await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 2, 'SIGNALED', 9)),
    dmesg: '[ 812.123456] Out of memory: Killed process 4242 (com.example.game) total-vm:9000000kB' }),
  { kind: 'stopped', reason: 'kernel out-of-memory kill', status: 9 });
  assert.equal((await gameExit(t, { exitInfo: dump(record('2026-09-22 18:14:57.923', 4242, 2, 'SIGNALED', 6)) })).kind, 'crashed');
});

test("an earlier session's record for a reused pid is not this session's", { skip: process.platform !== 'win32' }, async t => {
  const earlier = record('2026-09-21 09:00:00.000', 4242, 5, 'APP CRASH(NATIVE)', 11);
  assert.deepEqual(await gameExit(t, { exitInfo: dump(earlier), known: ['timestamp=2026-09-21 09:00:00.000 pid=4242'] }),
    { kind: 'unknown', reason: null, status: null });
  const current = record('2026-09-22 18:14:57.923', 4242, 1, 'EXIT_SELF', 0);
  assert.equal((await gameExit(t, { exitInfo: dump(current, earlier), known: ['timestamp=2026-09-21 09:00:00.000 pid=4242'] })).kind, 'exited');
});
