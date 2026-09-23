param(
    [string]$GameName,
    [string]$AppApk,
    [string]$RuntimeApk,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Avd = 'axrb-nvidia-api34',
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_.]+$')][string]$Package,
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_./]+$')][string]$Activity,
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [ValidateRange(2048, 16384)][int]$MemoryMB = 8192,
    [ValidateRange(2, 6)][int]$CpuCores = 4,
    [ValidateSet('Auto', 'Default', 'Tsc', 'TscCorrected')][string]$GuestClock = 'Auto',
    [ValidateSet('Auto', 'Off')][string]$UnrealMemoryPolicy = 'Auto',
    [ValidateSet(128, 1024)][int]$StorageReadAheadKB = 1024,
    [switch]$GpuSharing,
    [switch]$FpsHud,
    [ValidatePattern('^Local\\AXRB\.FpsHud\.[a-f0-9]{32}$')][string]$FpsHudEventName,
    [switch]$PrecomposeProjectionLayers,
    [switch]$OwnsEmulator,
    [string]$HostExe
)
# The launcher reads this script's output as UTF-8 and reports a failure from
# its message alone, so progress records stay out of the stream and a
# terminating error is reduced to its text instead of a full error record. The
# trap still lets the finally blocks below run before the process exits.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
. "$PSScriptRoot/../paths.ps1"
if ($AxrbPortableRoot) {
    if ($PSBoundParameters.ContainsKey('Sdk') -and [IO.Path]::GetFullPath($Sdk) -ne $env:ANDROID_HOME) {
        throw 'Portable launches must use the managed SDK inside the portable folder.'
    }
    $Sdk = $env:ANDROID_HOME
    if (!$PSBoundParameters.ContainsKey('Avd')) { $Avd = $(if ($AxrbPortableSettings.avd) { $AxrbPortableSettings.avd } else { 'axrb-managed-api36' }) }
    if (!$PSBoundParameters.ContainsKey('Port')) { $Port = $(if ($AxrbPortableSettings.port) { $AxrbPortableSettings.port } else { 5584 }) }
}
$env:ANDROID_ADB_SERVER_PORT = '5038'
$env:ADB_SERVER_SOCKET = $null
if (!$HostExe) { $HostExe = $AxrbHostExe }
if (!$PSBoundParameters.ContainsKey('GpuSharing')) {
    $GpuSharing = Test-Path "$AxrbGpuDirectory/axrb_gpu_layer.json"
}
if (([string]$Activity).Split('/')[0] -ne $Package) { throw 'Activity must belong to Package.' }
$HostExe = (Resolve-Path -LiteralPath $HostExe).Path
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$serial = "emulator-$Port"
if ($GuestClock -eq 'Auto') {
    $GuestClock = 'Default'
    $clockTools = $AxrbClockDirectory
    $qemu = Join-Path $Sdk 'emulator/qemu/windows-x86_64/qemu-system-x86_64-headless.exe'
    if ((Test-Path "$clockTools/axrb_clock_launcher.exe") -and (Test-Path "$clockTools/axrb_whpx_clock.dll") -and
        (Test-Path $qemu) -and (Get-FileHash -LiteralPath $qemu -Algorithm SHA256).Hash -eq
        'DCEC1CC23AC57FF04EC748CDE7E42BFC713BF2AD532E49606A4A9332CFB94B56') { $GuestClock = 'TscCorrected' }
}
$logs = Join-Path $AxrbOut 'logs/game'
New-Item -ItemType Directory -Force -Path $logs | Out-Null

# All ADB operations are bounded so an unresponsive guest cannot strand the window.
function Invoke-Adb([string[]]$Arguments, [int]$TimeoutMs = 10000) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $adb
    $info.Arguments = $(if ($Arguments[0] -eq 'devices') { $Arguments -join ' ' } else { (@('-s', $serial) + $Arguments) -join ' ' })
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $child = [System.Diagnostics.Process]::Start($info)
    if ($null -eq $child) { throw 'ADB could not be started.' }
    try {
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        if (!$child.WaitForExit($TimeoutMs)) { $child.Kill(); throw 'ADB timed out.' }
        [string]$stdoutText = $stdout.Result
        [string]$stderrText = $stderr.Result
        return @{ Code = $child.ExitCode; Text = $stdoutText.Trim(); Error = $stderrText.Trim() }
    } finally { $child.Dispose() }
}

# Play preparation may have started Android before this script runs, so a
# present device never implies a user-owned emulator. Ownership arrives
# explicitly and only this session's emulator is ever shut down below.
$ownsEmulator = [bool]$OwnsEmulator
$bridgeProcess = $null
$gameStarted = $false
$closeRequest = $null
$closeReady = $null
$fpsHudEvent = $null
try {
    if (!$FpsHudEventName) { $FpsHudEventName = 'Local\AXRB.FpsHud.' + [guid]::NewGuid().ToString('N') }
    $fpsHudEvent = [System.Threading.EventWaitHandle]::new([bool]$FpsHud, [System.Threading.EventResetMode]::ManualReset, $FpsHudEventName)
    # get-state can wait indefinitely for an absent serial on newer ADB.
    $device = Invoke-Adb @('devices')
    if ($device.Code -ne 0 -or $device.Text -notmatch ('(?m)^' + [regex]::Escape($serial) + '\s+device\s*$')) {
        $cpuArgs = @{}
        if ($PSBoundParameters.ContainsKey('CpuCores')) { $cpuArgs.CpuCores = $CpuCores }
        & "$PSScriptRoot\..\emulator\windows_android_emulator.ps1" @cpuArgs -Action Start -Sdk $Sdk -Avd $Avd -Port $Port -Abi arm64-v8a -GpuSharing:$GpuSharing -MemoryMB $MemoryMB -GuestClock $GuestClock
        $ownsEmulator = $true
    } elseif ($PSBoundParameters.ContainsKey('Avd')) {
        $runningAvd = Invoke-Adb @('emu', 'avd', 'name')
        [string]$runningText = $runningAvd.Text
        $runningName = ($runningText -split '\r?\n')[0].Trim()
        if ($runningAvd.Code -ne 0 -or $runningName -ne $Avd) {
            throw "$serial is running AVD '$runningName', but '$Avd' was requested. Stop that emulator first or use another port."
        }
    }
    if ($PSBoundParameters.ContainsKey('CpuCores')) {
        $actualCores = Invoke-Adb @('shell', 'getconf', '_NPROCESSORS_ONLN')
        if ($actualCores.Code -ne 0 -or $actualCores.Text -ne "$CpuCores") {
            throw 'Restart Android to apply the selected vCPU count.'
        }
    }
    foreach ($apk in @($RuntimeApk, $AppApk)) {
        if ([string]::IsNullOrWhiteSpace($apk)) { continue }
        $apkPath = (Resolve-Path -LiteralPath $apk).Path
        & $adb -s $serial install --no-incremental --force-queryable -r $apkPath
        if ($LASTEXITCODE -ne 0) { throw "APK install failed: $apkPath" }
    }
    $installed = Invoke-Adb @('shell', 'pm', 'path', $Package)
    if ($installed.Code -ne 0 -or $installed.Text -notmatch '^package:') { throw "$Package is not installed." }
    $policyArgs = @("$PSScriptRoot\..\emulator\unreal_memory_policy.py", '--sdk', $Sdk, '--serial', $serial, '--package', $Package)
    if ($UnrealMemoryPolicy -eq 'Off') { $policyArgs += '--restore' }
    $policyJson = & python @policyArgs
    if ($LASTEXITCODE -ne 0) { throw 'Unreal memory policy failed. The game was not started.' }
    Write-Output "Unreal memory policy: $policyJson"
    # Compatibility must be supplied by AXRB/Android, never by rewriting installed
    # application libraries. Keep the guest-wide mapping policy independent.
    $runtimePolicy = & python "$PSScriptRoot\..\emulator\android_runtime_policy.py" --sdk $Sdk --serial $serial --package $Package --storage-read-ahead-kib $StorageReadAheadKB
    if ($LASTEXITCODE -ne 0) { throw 'Android runtime policy failed. The game was not started.' }
    Write-Output "Android runtime policy: $runtimePolicy"
    # The audio adapter is allowed to fail without stopping the launch; say so
    # where a player will see it rather than only inside the policy report.
    $audioPolicy = $null
    try { $audioPolicy = (($runtimePolicy -join '') | ConvertFrom-Json).audio } catch { }
    if ($audioPolicy -and $audioPolicy.status -eq 'failed') { Write-Output "Warning: the audio adapter was not applied, so the game uses Android's stock audio driver. $($audioPolicy.reason)" }
    $appMetadataDir = Join-Path $logs "apps\$Package"
    $labelJson = & python "$PSScriptRoot\android_app_label.py" --sdk $Sdk --serial $serial --package $Package --icon-output "$appMetadataDir\icon.png"
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the installed APK metadata.' }
    $appMetadata = $labelJson | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($GameName)) {
        $GameName = $appMetadata.label
        Write-Output "APK display name: $GameName"
    }
    $steamManifest = Join-Path $appMetadataDir 'app.vrmanifest'
    $steamIdentity = $false
    $activeRuntime = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue).ActiveRuntime
    if ($activeRuntime -match 'steam') {
        $identityArgs = @("$PSScriptRoot\steamvr_app_identity.py", '--manifest', $steamManifest,
            '--package', $Package, '--name', $GameName, '--launcher', "$PSScriptRoot\run_windows_game.ps1",
            '--avd', $Avd, '--port', $Port, '--activity', $Activity)
        if ($appMetadata.icon) { $identityArgs += @('--icon', $appMetadata.icon) }
        $identityResult = & python @identityArgs
        $steamIdentity = $LASTEXITCODE -eq 0
        if ($steamIdentity) { Write-Output "SteamVR metadata: $identityResult" }
        else { Write-Output 'Warning: SteamVR metadata registration failed; using the OpenXR application name.' }
    }
    # Quote one Windows command-line argument, including embedded quotes and
    # trailing backslashes in APK labels. Never interpret the label as code.
    $titleArgument = '"' + [regex]::Replace([regex]::Replace($GameName, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    foreach ($bridgePort in 38490,38491) {
        $reverse = Invoke-Adb @('reverse', "tcp:$bridgePort", "tcp:$bridgePort")
        if ($reverse.Code -ne 0) { throw $reverse.Error }
    }
    $closeEventName = 'Local\AXRB.Close.' + [guid]::NewGuid().ToString('N')
    $closeRequest = [System.Threading.EventWaitHandle]::new($false, [System.Threading.EventResetMode]::ManualReset, $closeEventName)
    $closeReady = [System.Threading.EventWaitHandle]::new($false, [System.Threading.EventResetMode]::ManualReset, "$closeEventName.ready")
    $previousCloseEvent = $env:AXRB_CLOSE_EVENT
    $previousFpsHudEvent = $env:AXRB_FPS_HUD_EVENT
    $previousPrecomposeProjectionLayers = $env:AXRB_PRECOMPOSE_PROJECTION_LAYERS
    try {
        $env:AXRB_CLOSE_EVENT = $closeEventName
        $env:AXRB_FPS_HUD_EVENT = $FpsHudEventName
        $env:AXRB_PRECOMPOSE_PROJECTION_LAYERS = $(if ($PrecomposeProjectionLayers) { '1' } else { '0' })
        $bridgeProcess = Start-Process -FilePath $HostExe -ArgumentList @('--serve-openxr', '38490', '0', $titleArgument) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\host.log" -RedirectStandardError "$logs\host.err"
    } finally {
        $env:AXRB_CLOSE_EVENT = $previousCloseEvent
        $env:AXRB_FPS_HUD_EVENT = $previousFpsHudEvent
        $env:AXRB_PRECOMPOSE_PROJECTION_LAYERS = $previousPrecomposeProjectionLayers
    }
    Start-Sleep -Milliseconds 800
    if ($bridgeProcess.HasExited) {
        # The old message pointed at a relative path that does not exist in an
        # installed build, and withheld the one line that explains the failure.
        $reason = ''
        if (Test-Path -LiteralPath "$logs\host.err") {
            $reason = ([string]((Get-Content -LiteralPath "$logs\host.err" -Tail 6 -ErrorAction SilentlyContinue) -join ' ')).Trim()
        }
        if (!$reason) { $reason = 'The host wrote no diagnostics; SteamVR or another OpenXR runtime with a connected headset is required.' }
        throw "Host failed to start: $reason Full log: $logs\host.err"
    }
    if ($steamIdentity) {
        $identityResult = & python "$PSScriptRoot\steamvr_app_identity.py" --manifest $steamManifest --package $Package --pid $bridgeProcess.Id
        if ($LASTEXITCODE -eq 0) { Write-Output "SteamVR identity: $identityResult" }
        else { Write-Output 'Warning: SteamVR process identification failed; using the OpenXR application name.' }
    }
    $launch = Invoke-Adb @('shell', 'am', 'start', '-W', '-n', $Activity) 60000
    if ($launch.Code -ne 0 -or $launch.Text -match 'Error:') { throw "Game launch failed: $($launch.Text) $($launch.Error)" }
    $gameStarted = $true
    Write-Output "$GameName | AXRB is running. Closing its window stops this game session."
    $missing = 0
    while (!$bridgeProcess.HasExited) {
        if ($closeRequest.WaitOne(0)) { break }
        $game = Invoke-Adb @('shell', 'pidof', $Package)
        if ($game.Code -eq 0 -and $game.Text) { $missing = 0 } else { $missing++ }
        if ($missing -ge 2) { break }
        Start-Sleep -Milliseconds 500
    }
} finally {
    if ($gameStarted) {
        # Give the activity its normal onPause/onStop callbacks while rendering
        # and pose transport are still alive. sync alone cannot flush app memory.
        try {
            $pause = Invoke-Adb @('shell', 'am', 'start', '-W', '-a', 'android.intent.action.MAIN', '-c', 'android.intent.category.HOME') 10000
            if ($pause.Code -ne 0 -or $pause.Text -match 'Error:') { throw "Android pause failed: $($pause.Text) $($pause.Error)" }
            Start-Sleep -Seconds 2
            $flush = Invoke-Adb @('shell', 'sync')
            if ($flush.Code -ne 0) { throw "Android sync failed: $($flush.Error)" }
            Write-Output 'Android activity backgrounded and filesystem flushed before shutdown.'
        } catch { Write-Output "Warning: save/pause did not complete: $_" }
    }
    if ($closeReady) { $null = $closeReady.Set() }
    if ($bridgeProcess -and !$bridgeProcess.HasExited) {
        # WM_CLOSE lets the host leave its own main loop; force only after timeout.
        $null = $bridgeProcess.CloseMainWindow()
        if (!$bridgeProcess.WaitForExit(5000)) { $bridgeProcess.Kill() }
    }
    if ($ownsEmulator) {
        # North Star's force-stop can crash Gfxstream. A session-owned emulator
        # is shut down as a whole, which also terminates the Android game.
        try { $null = Invoke-Adb @('shell', 'sync'); $null = Invoke-Adb @('emu', 'kill') } catch { Write-Output "Warning: $_" }
    } elseif ($gameStarted) {
        try { $null = Invoke-Adb @('shell', 'am', 'force-stop', $Package) } catch { Write-Output "Warning: $_" }
    }
    if ($bridgeProcess) { $bridgeProcess.Dispose() }
    if ($closeRequest) { $closeRequest.Dispose() }
    if ($closeReady) { $closeReady.Dispose() }
    if ($fpsHudEvent) { $fpsHudEvent.Dispose() }
    Write-Output 'AXRB game session stopped.'
}
