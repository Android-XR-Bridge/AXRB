param(
 [ValidateSet('Start','Run')][string]$Action='Run',
 [ValidateRange(2,6)][int]$CpuCores=4,
 [ValidateSet('Keep','pipe','asg')][string]$GraphicsTransport='Keep',
 [ValidateSet('two-gear','lite-translate-or-interpret')][string]$TranslatorMode='two-gear',
 [switch]$QuietTranslator,
 [switch]$PollIdle,
 [switch]$DisableIrValidation
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$managed=Join-Path $env:LOCALAPPDATA 'AXRB Runtime'
$env:ANDROID_AVD_HOME=Join-Path $managed 'avd'
$env:ANDROID_USER_HOME=Join-Path $managed 'android'
$env:ANDROID_EMULATOR_HOME=$env:ANDROID_USER_HOME
$env:AXRB_DATA_HOME=Join-Path $root 'out/digitalis-test'
$sdk=Join-Path $managed 'sdk'
if($Action -eq 'Start') {
 if($GraphicsTransport -ne 'Keep') {
  $running=Get-CimInstance Win32_Process | Where-Object {$_.Name -like 'qemu-system*' -and $_.CommandLine -match 'axrb-digitalis-test'}
  if($running){throw 'Stop the isolated emulator before changing its graphics transport.'}
  $config=Join-Path $env:ANDROID_AVD_HOME 'axrb-digitalis-test.avd/config.ini'
  $text=[IO.File]::ReadAllText($config)
  if($text -notmatch '(?m)^AvdId=axrb-digitalis-test\r?$'){throw 'Unexpected test AVD config'}
  $backup="$config.before-transport-test"
  if(!(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $config -Destination $backup}
  $text=[regex]::Replace($text,'(?m)^hw\.gltransport=.*\r?\n?','')
  [IO.File]::WriteAllText($config,$text.TrimEnd()+"`r`nhw.gltransport=$GraphicsTransport`r`n",[Text.UTF8Encoding]::new($false))
 }
 & "$root/scripts/emulator/windows_android_emulator.ps1" -Action Start -Sdk $sdk -Avd axrb-digitalis-test -Port 5586 -ApiLevel 36 -Abi arm64-v8a -MemoryMB 8192 -CpuCores $CpuCores -GuestClock TscCorrected -GpuSharing -ColdBoot -PollIdle:$PollIdle
} else {
 if($PollIdle){throw 'PollIdle is a boot setting; use -Action Start.'}
 if($GraphicsTransport -ne 'Keep'){throw 'GraphicsTransport is a boot setting; use -Action Start.'}
 $adb=Join-Path $sdk 'platform-tools/adb.exe'
 $bridge=& $adb -P 5038 -s emulator-5586 shell getprop ro.dalvik.vm.native.bridge
 if($LASTEXITCODE -or ([string]$bridge).Trim() -ne 'libberberis_arm64.so'){throw 'Digitalis is not active on the test emulator.'}
 $avdName=& $adb -P 5038 -s emulator-5586 emu avd name
 if($LASTEXITCODE -or $avdName -notcontains 'axrb-digitalis-test'){throw 'Isolated Digitalis AVD required.'}
 $active=& $adb -P 5038 -s emulator-5586 shell pidof com.Ubisoft.ACNexusVR
 if($active){throw 'Close the existing Nexus session before changing translator settings.'}
 & $adb -P 5038 -s emulator-5586 root
 & $adb -P 5038 -s emulator-5586 wait-for-device
 $saved=@{}
 foreach($key in @('berberis.mode','berberis.flags','log.tag.berberis')) {
  $saved[$key]=[string](& $adb -P 5038 -s emulator-5586 shell getprop $key)
  if($LASTEXITCODE){throw "Cannot read $key"}
 }
 try {
  & $adb -P 5038 -s emulator-5586 shell "setprop wrap.com.Ubisoft.ACNexusVR ''; setprop berberis.mode $TranslatorMode"
  if($LASTEXITCODE){throw 'Could not configure the test translator.'}
  if($QuietTranslator){& $adb -P 5038 -s emulator-5586 shell setprop log.tag.berberis W; if($LASTEXITCODE){throw 'Cannot configure logging'}}
  if($DisableIrValidation){
   $flags=$saved['berberis.flags']
   if(!$flags){$flags=[string](& $adb -P 5038 -s emulator-5586 shell getprop ro.berberis.flags)}
   $flags=(@($flags -split ',' | Where-Object {$_})+'disable-ir-check' | Select-Object -Unique) -join ','
   & $adb -P 5038 -s emulator-5586 shell setprop berberis.flags $flags
   if($LASTEXITCODE){throw 'Cannot configure IR validation'}
  }
  & "$root/scripts/run/run_windows_game.ps1" -Package com.Ubisoft.ACNexusVR -Activity com.Ubisoft.ACNexusVR/com.unity3d.player.UnityPlayerActivity -Sdk $sdk -Avd axrb-digitalis-test -Port 5586 -MemoryMB 8192 -CpuCores $CpuCores -GuestClock TscCorrected -GpuSharing -FpsHud -CollectGuestCpu -CaptureGuestLog -HostExe "$root/out/host/bin/Release/axrb-host-bridge.exe"
 } finally {
  foreach($key in $saved.Keys){
   & $adb -P 5038 -s emulator-5586 shell "setprop $key '$($saved[$key])'"
   if($LASTEXITCODE){Write-Warning "Could not restore $key"}
  }
 }
}
