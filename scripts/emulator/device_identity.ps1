# Derived from Refract scripts/device_identity.ps1 (MIT).
# Copyright (c) 2026 Refract contributors; Copyright (c) 2026 Florian Reintgen (AXRB).
# See licenses/Refract-device-identity-MIT.txt for the full license notice.
param([Parameter(Mandatory)][ValidateSet('quest', 'google')][string]$Identity, [ValidatePattern('^emulator-[0-9]+$')][string]$Serial = 'emulator-5580',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(1024,65535)][int]$AdbServerPort = 5038)
# Switches the emulator's brand/manufacturer between a Quest ('oculus'/'Oculus') and 'google'/'Google'.
# The model stays "Quest 3" either way. Reboots the guest; recheck hardware rendering afterwards.
#   quest  - unmodified Quest APKs: Unity's OculusUnity.getIsOnOculusHardware() needs "oculus" in
#            Build.MANUFACTURER, and the Platform SDK then comes from the Refract platform package.
#   google - OVRPort-patched APKs that must pick OVRPort's generic OpenXR loader.
# The first switch saves each build.prop as <file>.before-identity next to it.
$ErrorActionPreference = 'Stop'
$env:ANDROID_ADB_SERVER_PORT = [string]$AdbServerPort
$env:ADB_SERVER_SOCKET = $null
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
if (!(Test-Path -LiteralPath $adb)) { throw 'SDK platform-tools/adb.exe is missing.' }
& $adb -s $Serial get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Start the selected emulator before changing its identity.' }
$brand, $manufacturer = if ($Identity -eq 'quest') { 'oculus', 'Oculus' } else { 'google', 'Google' }
$files = '/system/build.prop /vendor/build.prop /product/etc/build.prop /system_ext/etc/build.prop /odm/etc/build.prop'

& $adb -s $Serial root | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ADB root failed." }
& $adb -s $Serial wait-for-device
& $adb -s $Serial remount | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ADB remount failed." }
$script = "set -e; for f in $files; do [ -f `$f ] || continue; [ -f `$f.before-identity ] || cp -p `$f `$f.before-identity; " +
          "sed -i -E 's/^(ro\.product\.[a-z_]+\.brand)=.*/\1=$brand/; s/^(ro\.product\.[a-z_]+\.manufacturer)=.*/\1=$manufacturer/' `$f; done; " +
          "grep -hE '^ro\.product\.[a-z_]+\.(brand|manufacturer)=' $files"
& $adb -s $Serial shell $script
if ($LASTEXITCODE -ne 0) { throw "Guest identity update failed; inspect the preserved build-property backups." }
& $adb -s $Serial reboot
if ($LASTEXITCODE -ne 0) { throw "Guest reboot failed." }
& $adb -s $Serial wait-for-device
& $adb -s $Serial shell 'while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 1; done; getprop ro.product.brand; getprop ro.product.manufacturer'
