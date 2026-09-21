$AxrbRoot = Split-Path $PSScriptRoot -Parent
$AxrbOut = Join-Path $AxrbRoot 'out'
$AxrbAssets = $AxrbOut
if ($env:AXRB_DATA_HOME) { $AxrbOut = $env:AXRB_DATA_HOME }
$AxrbHostExe = Join-Path $AxrbAssets 'host/bin/Release/axrb-host-bridge.exe'
$AxrbGpuDirectory = Join-Path $AxrbAssets 'gpu/Release'
$AxrbClockDirectory = Join-Path $AxrbAssets 'clock/Release'
$AxrbKeys = Join-Path $AxrbRoot '.local/keys'
# A keystore dropped at the checkout root is the signing identity for every APK
# this repository builds. Clean checkouts have none, so fall back to the
# generated debug key under .local; both paths are gitignored.
$AxrbKeystore = Join-Path $AxrbRoot 'runtime.keystore'
if (!(Test-Path -LiteralPath $AxrbKeystore)) { $AxrbKeystore = Join-Path $AxrbKeys 'runtime.keystore' }
