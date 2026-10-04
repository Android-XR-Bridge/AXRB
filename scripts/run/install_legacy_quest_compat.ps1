# SPDX-License-Identifier: MIT
param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(5554,5682)][int]$Port = 5580,
    [ValidatePattern('^[a-zA-Z0-9_.]+$')][string]$Package = 'com.camouflaj.salmon',
    [string]$DriverApk,
    [string]$RuntimeApk,
    [ValidateRange(1024,65535)][int]$AdbServerPort = 5038,
    [string]$PlatformApk,
    [switch]$ControllersOnly
)
$ErrorActionPreference = 'Stop'
$env:ANDROID_ADB_SERVER_PORT = [string]$AdbServerPort
$env:ADB_SERVER_SOCKET = $null
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (!$DriverApk) { $DriverApk = Join-Path $root 'out/android/runtime-arm64-v8a/axrb-systemdriver-debug.apk' }
if (!$RuntimeApk) { $RuntimeApk = Join-Path $root 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk' }
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$serial = "emulator-$Port"
function Run-Adb([string[]]$Arguments) {
    & $adb -s $serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ADB operation failed on $serial." }
}
if (!(Test-Path -LiteralPath $adb)) { throw 'SDK platform-tools/adb.exe is missing.' }
if (!(Test-Path -LiteralPath $RuntimeApk)) { throw 'The matching ARM64 runtime APK is missing; build it or pass -RuntimeApk.' }
if (!(Test-Path -LiteralPath $DriverApk)) { throw 'Build the ARM64 runtime and legacy driver first, or pass -DriverApk.' }
$state = & $adb -s $serial get-state
if ($LASTEXITCODE -ne 0 -or ($state -join '').Trim() -ne 'device') { throw 'Start the selected emulator and retry.' }
$running = & $adb -s $serial shell pidof $Package
if (($running -join '').Trim()) { throw 'Stop the game before installing runtime packages or changing its input policy.' }
# Only emulator guests are addressed. No platform ownership state is modified.
Run-Adb @('install', '--no-incremental', '--force-queryable', '-r', (Resolve-Path -LiteralPath $RuntimeApk).Path)
Run-Adb @('install', '--no-incremental', '--force-queryable', '-r', (Resolve-Path -LiteralPath $DriverApk).Path)
if ($PlatformApk) {
    Run-Adb @('install', '--no-incremental', '--force-queryable', '-r', (Resolve-Path -LiteralPath $PlatformApk).Path)
}
$property = "persist.sys.axrb.legacy.controllers_only.$Package"
$value = if ($ControllersOnly) { '1' } else { '0' }
Run-Adb @('shell', 'setprop', $property, $value)
$actual = & $adb -s $serial shell getprop $property
if ($LASTEXITCODE -ne 0 -or ($actual -join '').Trim() -ne $value) {
    throw 'Controller compatibility was not applied. A root-capable emulator is required; run adb root and retry.'
}
Write-Host "Legacy driver installed. Controller-only policy for $Package is $value; restart the game."
