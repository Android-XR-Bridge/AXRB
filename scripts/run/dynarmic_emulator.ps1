param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_.]+$')][string]$Package,
    [ValidateSet('Enable','Disable')][string]$Action='Enable',
    [string]$Adb=(Join-Path $env:LOCALAPPDATA 'AXRB Runtime/sdk/platform-tools/adb.exe'),
    [int]$AdbServerPort=5038,
    [string]$Serial='emulator-5584',
    [switch]$LegacyFallback,
    [switch]$ProfileFallback
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Invoke-Adb([string[]]$Arguments) {
    $result=& $Adb -P $AdbServerPort -s $Serial @Arguments
    if($LASTEXITCODE -ne 0){throw "ADB failed: $($result -join ' ')"}
    $result
}
$state=Invoke-Adb @('get-state')
if($state -ne 'device'){throw 'Start the Android emulator first.'}
# Replacing a mapped .so can corrupt an active process, including a different
# app using this adapter. Deployment uses a distinct content-addressed filename.
$running=& $Adb -P $AdbServerPort -s $Serial shell pidof $Package
if($running){throw "Close $Package before changing its translator."}
Invoke-Adb @('root')
Invoke-Adb @('wait-for-device')
if($Action -eq 'Disable') {
    Invoke-Adb @('shell',"setprop wrap.$Package ''")
    Write-Output "Default Android translator restored for $Package."
    return
}
$library=Join-Path $root 'out/dynarmic-android/libaxrb_dynarmic_bridge.so'
if(!(Test-Path $library)){throw 'Build the Android Dynarmic experiment first with scripts/build/test_dynarmic.ps1 -Android.'}
$hash=(Get-FileHash $library -Algorithm SHA256).Hash.ToLowerInvariant()
$remote='/data/local/tmp/axrb-dynarmic-probe'
$remoteLibrary="$remote/libaxrb_dynarmic_bridge-$hash.so"
$remoteWrapper="$remote/wrap-$Package.sh"
Invoke-Adb @('shell',"mkdir -p $remote")
$present=& $Adb -P $AdbServerPort -s $Serial shell "test -f $remoteLibrary"
if($LASTEXITCODE -ne 0){Invoke-Adb @('push',$library,$remoteLibrary)}
$wrapper=(Get-Content (Join-Path $root 'experimental/dynarmic/wrap.sh') -Raw).Replace("$remote/libaxrb_dynarmic_bridge.so",$remoteLibrary)
if($LegacyFallback){$wrapper=$wrapper.Replace('export BERBERIS_MODE=',"export AXRB_DYNARMIC_LEGACY_FALLBACK=1`nexport BERBERIS_MODE=")}
if($ProfileFallback){$wrapper=$wrapper.Replace('export BERBERIS_MODE=',"export AXRB_DYNARMIC_PROFILE_FALLBACK=1`nexport BERBERIS_MODE=")}
$temporary=Join-Path $root "out/dynarmic-android/wrap-$Package.sh"
[IO.File]::WriteAllText($temporary,$wrapper.Replace("`r`n","`n"),[Text.UTF8Encoding]::new($false))
Invoke-Adb @('push',$temporary,$remoteWrapper)
Invoke-Adb @('shell',"chmod 755 $remoteWrapper; chcon u:object_r:system_file:s0 $remoteWrapper; chcon u:object_r:system_lib_file:s0 $remoteLibrary")
Invoke-Adb @('shell',"setprop wrap.$Package $remoteWrapper")
$actual=Invoke-Adb @('shell','getprop',"wrap.$Package")
if($actual -ne $remoteWrapper){throw 'Android did not apply the translator wrapper.'}
Write-Output "Experimental Dynarmic adapter enabled for $Package. Launch normally from AXRB. The existing bridge still supplies Android ABI integration and instruction fallback."
