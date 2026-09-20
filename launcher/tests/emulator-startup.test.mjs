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
