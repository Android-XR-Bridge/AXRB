import test from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { run } from '../core/runtime.mjs';

const script = fileURLToPath(new URL('../../scripts/emulator/windows_android_emulator.ps1', import.meta.url)).replaceAll("'", "''");
const windows = { skip: process.platform !== 'win32', timeout: 15000 };

// Execute the production functions/diagnostic block without starting an emulator.
// Only the external ADB boundary and retry delay are replaced.
async function probe(body) {
  const setup = `
$ErrorActionPreference = 'Stop'
$ast = [System.Management.Automation.Language.Parser]::ParseFile('${script}', [ref]$null, [ref]$null)
foreach ($name in @('Invoke-Adb', 'Verify-Abi')) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Start-Sleep { }
$adb = 'fixture-adb'
$serial = 'emulator-5584'
${body}
`;
  const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-EncodedCommand', Buffer.from(setup, 'utf16le').toString('base64')]);
  return JSON.parse(output.match(/^RESULT:(.*)$/m)?.[1] ?? 'null');
}

test('recovered ADB reads preserve JSON and still reject a disabled ARM bridge', windows, async () => {
  const result = await probe(`
$script:vkAttempts = 0
$script:bridgeAttempts = 0
function Invoke-ExternalWithTimeout($Exe, $Arguments, $TimeoutSeconds) {
    switch ($Arguments[-1]) {
        'vkjson' {
            $script:vkAttempts += 1
            if ($script:vkAttempts -eq 1) { throw 'device offline' }
            return '{"devices":[{"properties":{"deviceName":"fixture GPU"}}]}'
        }
        'ro.product.cpu.abilist' { return 'x86_64,arm64-v8a' }
        'ro.dalvik.vm.native.bridge' {
            $script:bridgeAttempts += 1
            if ($script:bridgeAttempts -eq 1) { throw 'device offline' }
            return '0'
        }
        default { throw 'Unexpected fixture command' }
    }
}
$deviceCount = 0
try { $raw = Invoke-Adb @('shell', 'cmd', 'gpu', 'vkjson'); $deviceCount = @(($raw -join "\n" | ConvertFrom-Json).devices).Count } catch { }
$Abi = 'arm64-v8a'
$bridgeRejected = $false
try { Verify-Abi | Out-Null } catch { $bridgeRejected = $_.Exception.Message -match 'requires an enabled native bridge' }
Write-Output ('RESULT:' + (@{ deviceCount = $deviceCount; bridgeRejected = $bridgeRejected } | ConvertTo-Json -Compress))
`);
  assert.deepEqual(result, { deviceCount: 1, bridgeRejected: true });
});

test('offline errors trigger reconnect then server recovery, unlike an absent device', windows, async () => {
  const result = await probe(`
$diagnostic = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -match '\\$lastDiagnostic'
}, $true)
$script:calls = [System.Collections.Generic.List[string]]::new()
$script:failure = 'adb: error: device offline (exit 1)'
function Invoke-ExternalWithTimeout($Exe, $Arguments, $TimeoutSeconds) {
    if ($Arguments -contains 'get-state') { throw $script:failure }
    if ($Arguments[0] -eq 'devices') { return 'emulator-5584 offline' }
    $script:calls.Add(($Arguments -join ' '))
    return ''
}
$process = [pscustomobject]@{ HasExited = $false }
$startedAt = (Get-Date).AddSeconds(-100)
$devicesReported = $false
$adbReconnectAttempted = $false
$adbServerRestarted = $false
$bootFailure = ''; $bootText = ''
foreach ($attempt in 1..2) {
    $lastDiagnostic = (Get-Date).AddSeconds(-31)
    . ([scriptblock]::Create($diagnostic.Extent.Text))
}
$script:failure = "adb: device 'emulator-5584' not found (exit 1)"
$adbReconnectAttempted = $false
$adbServerRestarted = $false
$lastDiagnostic = (Get-Date).AddSeconds(-31)
. ([scriptblock]::Create($diagnostic.Extent.Text))
Write-Output ('RESULT:' + (ConvertTo-Json -InputObject @($script:calls.ToArray()) -Compress))
`);
  assert.deepEqual(result, ['reconnect offline', 'kill-server', 'start-server']);
});

test('stale cleanup requests graceful shutdown only for the requested emulator and port owner', windows, async () => {
  const result = await probe(`
foreach ($name in @('Get-ManagedEmulatorProcess', 'Stop-StaleManagedEmulator')) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
}
$Sdk = 'C:\\fixture\\sdk'
$Avd = 'axrb-managed-api36'
$Port = 5584
$script:commands = [System.Collections.Generic.List[string]]::new()
$script:listenerOwner = 11
# Start-Process -ArgumentList quotes every array element individually, so the
# real Win32 command line reads "emulator.exe" "-avd" "name" "-ports" "a,b"
# rather than the space-separated shape it is easy to assume while writing a
# fixture by hand. Using that real shape here is what makes this test able to
# catch a matcher that only works against the shape someone imagined.
$script:processes = @(
  [pscustomobject]@{ ProcessId = 11; Name = 'emulator.exe'; ExecutablePath = 'C:\\fixture\\sdk\\emulator\\emulator.exe'; CommandLine = '"emulator.exe" "-avd" "axrb-managed-api36" "-ports" "5584,5585"' },
  [pscustomobject]@{ ProcessId = 12; Name = 'emulator.exe'; ExecutablePath = 'C:\\other\\emulator\\emulator.exe'; CommandLine = '"emulator.exe" "-avd" "axrb-managed-api36" "-ports" "5584,5585"' },
  [pscustomobject]@{ ProcessId = 13; Name = 'emulator.exe'; ExecutablePath = 'C:\\fixture\\sdk\\emulator\\emulator.exe'; CommandLine = '"emulator.exe" "-avd" "axrb-managed-api36" "-ports" "5586,5587"' }
)
function Get-CimInstance { param($ClassName, $Filter, $ErrorAction)
  if ($Filter) { return $script:processes | Where-Object { $Filter -eq "ProcessId=$($_.ProcessId)" } }
  return $script:processes
}
function Get-NetTCPConnection { return [pscustomobject]@{ OwningProcess = $script:listenerOwner } }
function Invoke-ExternalWithTimeout { param($Exe, $Arguments, $TimeoutSeconds)
  $script:commands.Add(($Arguments -join ' '))
  $script:processes = @($script:processes | Where-Object { $_.ProcessId -ne 11 })
  return ''
}
function Stop-Process { throw 'Force stop must not be called' }
Stop-StaleManagedEmulator -RequireCandidate
$script:listenerOwner = 99
$script:processes = @([pscustomobject]@{ ProcessId = 11; Name = 'emulator.exe'; ExecutablePath = 'C:\\fixture\\sdk\\emulator\\emulator.exe'; CommandLine = '"emulator.exe" "-avd" "axrb-managed-api36" "-ports" "5584,5585"' })
$rejected = $false
try { Stop-StaleManagedEmulator -RequireCandidate | Out-Null } catch { $rejected = $_.Exception.Message -match 'belongs to another process' }
Write-Output ('RESULT:' + (@{ commands = @($script:commands.ToArray()); rejected = $rejected } | ConvertTo-Json -Compress))
`);
  assert.deepEqual(result, { commands: ['-s emulator-5584 emu kill'], rejected: true });
});

test('Status action is side-effect-free and never throws for an unset-up SDK', windows, async () => {
  const output = await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-ExecutionPolicy', 'Bypass', '-File', script,
    '-Action', 'Status', '-Sdk', 'C:\\fixture\\does-not-exist', '-Avd', 'axrb-managed-api36', '-Port', '5584']);
  const result = JSON.parse(output.trim().split(/\r?\n/).filter(Boolean).pop());
  assert.deepEqual(result, { running: false, processId: null, count: 0, adbState: '' });
});

test('Status action reports the managed process and adb reachability together', windows, async () => {
  const result = await probe(`
$definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-ManagedEmulatorProcess' }, $true)
. ([scriptblock]::Create($definition.Extent.Text))
$switchStatement = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.SwitchStatementAst] }, $true)
$statusClause = ($switchStatement.Clauses | Where-Object { $_.Item1.Extent.Text -eq 'Status' } | Select-Object -First 1).Item2
# The clause's extent already includes its own { } delimiters; recreating a
# scriptblock straight from that text parses them as a *nested* scriptblock
# literal (an unexecuted value) instead of a body to run, so strip them first.
$statusText = $statusClause.Extent.Text
$statusText = $statusText.Substring(1, $statusText.Length - 2)
$Sdk = 'C:\\fixture\\sdk'
$Avd = 'axrb-managed-api36'
$Port = 5584
$script:processes = @(
  [pscustomobject]@{ ProcessId = 11; Name = 'emulator.exe'; ExecutablePath = 'C:\\fixture\\sdk\\emulator\\emulator.exe'; CommandLine = '"emulator.exe" "-avd" "axrb-managed-api36" "-ports" "5584,5585"' }
)
function Get-CimInstance { param($ClassName, $Filter, $ErrorAction) return $script:processes }
function Test-Path { param($LiteralPath) $true }
function Invoke-ExternalWithTimeout { param($Exe, $Arguments, $TimeoutSeconds) return 'device' }
$json = . ([scriptblock]::Create($statusText))
Write-Output ('RESULT:' + $json)
`);
  assert.deepEqual(result, { running: true, processId: 11, count: 1, adbState: 'device' });
});
