param(
    [string]$GameName,
    [ValidateRange(0,6)][int]$UnityJobWorkers = 0,
    [ValidatePattern('^[-a-zA-Z0-9_. ]*$')][string]$UnityArguments = '',
    [switch]$CaptureGuestLog,
    [switch]$CollectGuestCpu,
    [ValidatePattern('^[a-zA-Z0-9_.]+:[VDIWEF]$')][string[]]$GuestLogTags = @('AXRB.Perf:I', 'AXRB.Pacing:I', 'AXRB.GPU:I', 'AXRB.Accel:I'),
    [uint64]$HostAffinityMask = 0,
    [switch]$RequireTrackingOrigin,
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
    [ValidateSet('Auto', 'Qemu', 'Hypervisor', 'HypervisorX2Apic')][string]$LocalApic = 'Auto',
    [ValidateSet('Auto', 'Off')][string]$UnrealMemoryPolicy = 'Auto',
    [ValidateSet(128, 1024)][int]$StorageReadAheadKB = 1024,
    [switch]$GpuSharing,
    [switch]$FpsHud,
    [ValidatePattern('^Local\\AXRB\.FpsHud\.[a-f0-9]{32}$')][string]$FpsHudEventName,
    [switch]$PrecomposeProjectionLayers,
    [switch]$OwnsEmulator,
    [string]$HostExe,
    [ValidatePattern('^[a-f0-9]{8}$')][string]$SessionId
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
$env:ADB_LOCAL_TRANSPORT_MAX_PORT = '5683'
$env:ADB_USB_LEGACY = '1'
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
    $info.Arguments = $(if ($Arguments[0] -in 'devices', 'start-server', 'reconnect') { $Arguments -join ' ' } else { (@('-s', $serial) + $Arguments) -join ' ' })
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

# pidof only says the game is gone. Android records why each process ended, so
# ask it: a Quit from the game's own menu must not read as a crash. Reason
# names can hold brackets themselves ("APP CRASH(NATIVE)"), hence the lazy
# match up to the field that follows.
function Get-ExitRecords {
    $info = $null
    try { $info = Invoke-Adb @('shell', 'dumpsys', 'activity', 'exit-info', $Package) } catch { return }
    if ($info.Code -ne 0) { return }
    [regex]::Matches($info.Text, '(timestamp=\S+ \S+ pid=(\d+))[^\r\n]*\r?\n\s*process=\S+ reason=(\d+) \((.*?)\)(?: subreason=\d+ \((.*?)\))? status=(-?\d+)')
}
# Records outlive reboots and an emulator that boots the same way reuses pids,
# so a record that already existed when the game started is never this one.
function Get-GameExit([string]$ProcessId, [string[]]$Known = @()) {
    for ($attempt = 0; $ProcessId -and $attempt -lt 3; $attempt++) {
        if ($attempt) { Start-Sleep -Seconds 1 }
        $record = @(Get-ExitRecords) | Where-Object { $_.Groups[2].Value -eq $ProcessId -and $Known -notcontains $_.Groups[1].Value } | Select-Object -First 1
        if (!$record) { continue }
        $reason = [int]$record.Groups[3].Value
        $status = [int]$record.Groups[6].Value
        $name = $record.Groups[4].Value
        if ($record.Groups[5].Success -and $record.Groups[5].Value -ne 'UNKNOWN') { $name += " / $($record.Groups[5].Value)" }
        # EXIT_SELF with status 0 is a normal exit; any other status is the
        # game giving up, for example on a failed engine start. A bare SIGKILL
        # is usually the game killing itself, which is how Unity's
        # Application.Quit ends, unless the kernel's OOM killer did it: lmkd
        # kills are recorded as LOW_MEMORY, the kernel's only in its log.
        $kind = switch ($reason) {
            1 { if ($status -eq 0) { 'exited' } else { $name += ", exit code $status"; 'failed' } }
            2 {
                if ($status -ne 9) { $name += ", signal $status"; 'crashed' }
                else {
                    $oom = $null
                    try { $oom = Invoke-Adb @('shell', 'su', '0', 'dmesg', '|', 'grep', '-w', "Killed.process.$ProcessId") } catch { }
                    if ($oom -and $oom.Code -eq 0 -and $oom.Text) { $name = 'kernel out-of-memory kill'; 'stopped' } else { 'exited' }
                }
            }
            { $_ -in 4, 5, 6, 7 } { 'crashed' }
            default { 'stopped' }
        }
        return [ordered]@{ kind = $kind; reason = $name; status = $status }
    }
    return [ordered]@{ kind = 'unknown'; reason = $null; status = $null }
}

# Play preparation may have started Android before this script runs, so a
# present device never implies a user-owned emulator. Ownership arrives
# explicitly and only this session's emulator is ever shut down below.
$ownsEmulator = [bool]$OwnsEmulator
$bridgeProcess = $null
$guestLogProcess = $null
$guestCpuProcess = $null
$gameStarted = $false
$closeRequested = $false
$gameLost = $false
$androidLost = $false
$adbError = ''
$gameExit = $null
$pauseSucceeded = $false
$syncSucceeded = $false
$sessionStartedAt = $null
$hostLost = $false
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
        & "$PSScriptRoot\..\emulator\windows_android_emulator.ps1" @cpuArgs -Action Start -Sdk $Sdk -Avd $Avd -Port $Port -Abi arm64-v8a -GpuSharing:$GpuSharing -MemoryMB $MemoryMB -GuestClock $GuestClock -LocalApic $LocalApic
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
    $previousCpuFile = $env:AXRB_PERFORMANCE_CPU_FILE
    $guestCpuFile = Join-Path $logs ("guest-cpu-" + [guid]::NewGuid().ToString("N") + ".txt")
    $previousCloseEvent = $env:AXRB_CLOSE_EVENT
    $previousFpsHudEvent = $env:AXRB_FPS_HUD_EVENT
    $previousPrecomposeProjectionLayers = $env:AXRB_PRECOMPOSE_PROJECTION_LAYERS
    try {
        if ($CollectGuestCpu) { $env:AXRB_PERFORMANCE_CPU_FILE = $guestCpuFile }
        $env:AXRB_CLOSE_EVENT = $closeEventName
        $env:AXRB_FPS_HUD_EVENT = $FpsHudEventName
        $env:AXRB_PRECOMPOSE_PROJECTION_LAYERS = $(if ($PrecomposeProjectionLayers) { '1' } else { '0' })
        $sessionStartedAt = Get-Date
        $bridgeProcess = Start-Process -FilePath $HostExe -ArgumentList @('--serve-openxr', '38490', '0', $titleArgument) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\host.log" -RedirectStandardError "$logs\host.err"
        if ($HostAffinityMask) { $bridgeProcess.ProcessorAffinity = [intptr]$HostAffinityMask }
    } finally {
        $env:AXRB_PERFORMANCE_CPU_FILE = $previousCpuFile
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
    # View enumeration happens very early in Android app startup and its
    # selected eye extent is immutable for the session. Do not launch the app
    # until the OpenXR host has completed initialization and its pose stream
    # is listening; otherwise the runtime's bounded extent query can time out
    # on the default 1024x1024 extent before the host publishes its pose frame.
    $poseServerReady = $false
    $poseReadyDeadline = [DateTime]::UtcNow.AddSeconds(45)
    $poseReadyLog = Join-Path $logs 'host.err'
    $poseReadyLine = 'AXRB TCP: listening on 0.0.0.0:38490'
    while ([DateTime]::UtcNow -lt $poseReadyDeadline) {
        if ($bridgeProcess.HasExited) { break }
        if (Test-Path -LiteralPath $poseReadyLog) {
            $poseServerReady = [bool](Select-String -LiteralPath $poseReadyLog -SimpleMatch $poseReadyLine -Quiet -ErrorAction SilentlyContinue)
            if ($poseServerReady) { break }
            $poseServerFailed = [bool](Select-String -LiteralPath $poseReadyLog -Pattern 'AXRB TCP: (socket creation failed|bind failed on port 38490|listen failed)' -Quiet -ErrorAction SilentlyContinue)
            if ($poseServerFailed) { throw "Host pose server failed to listen. Full log: $poseReadyLog" }
        }
        Start-Sleep -Milliseconds 100
    }
    if (!$poseServerReady) {
        throw "Host pose server did not become ready within 45 seconds. Full log: $poseReadyLog"
    }
    if ($steamIdentity) {
        $identityResult = & python "$PSScriptRoot\steamvr_app_identity.py" --manifest $steamManifest --package $Package --pid $bridgeProcess.Id
        if ($LASTEXITCODE -eq 0) { Write-Output "SteamVR identity: $identityResult" }
        else { Write-Output 'Warning: SteamVR process identification failed; using the OpenXR application name.' }
    }
    $knownExits = @(Get-ExitRecords | ForEach-Object { $_.Groups[1].Value })
    if ($RequireTrackingOrigin) {
        # Some games cache their initial floor conversion. Starting before
        # the host establishes LOCAL captures a resting-head origin instead.
        Write-Output 'Put on the headset in your playing posture. Waiting for stable tracking before starting the game.'
        $trackingDeadline = [DateTime]::UtcNow.AddMinutes(3)
        $trackingReadySince = $null
        while ($true) {
            $bridgeProcess.Refresh()
            if ($bridgeProcess.HasExited) { throw 'Host exited while waiting for headset tracking.' }
            if ($closeRequest.WaitOne(0)) { throw 'Closed while waiting for headset tracking.' }
            $trackingLines = @(Get-Content -LiteralPath "$logs\host.err" -ErrorAction SilentlyContinue)
            $originReady = [bool]($trackingLines | Select-String 'LOCAL origin in tracking world=.*flags=0xf')
            $lastState = $trackingLines | Select-String 'session state=(\d+)' | Select-Object -Last 1
            $visible = $lastState -and ([int]$lastState.Matches[0].Groups[1].Value -in 4,5)
            if ($originReady -and $visible) {
                if ($null -eq $trackingReadySince) { $trackingReadySince = [DateTime]::UtcNow }
                if (([DateTime]::UtcNow - $trackingReadySince).TotalSeconds -ge 2) { break }
            } else { $trackingReadySince = $null }
            if ([DateTime]::UtcNow -ge $trackingDeadline) { throw 'The game was not started: wear the headset and retry for a valid tracking origin.' }
            Start-Sleep -Milliseconds 200
        }
        Write-Output 'Headset origin is ready; starting the game with unchanged physical tracking scale.'
    }
    # Start capture after runtime preparation, which may restart adbd.
    if ($CaptureGuestLog) {
        $guestLogProcess = Start-Process -FilePath $adb -ArgumentList (@('-s', $serial, 'logcat', '-v', 'threadtime', '-T', '1', '*:W') + $GuestLogTags) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs/guest.log" -RedirectStandardError "$logs/guest.err"
    }
    $launchArgs = @('shell', 'am', 'start', '-W')
    $launchArgs += @('-n', $Activity)
    $unityCommandLine = @()
    if ($UnityJobWorkers -gt 0) { $unityCommandLine += "-job-worker-count $UnityJobWorkers" }
    if ($UnityArguments.Trim()) { $unityCommandLine += $UnityArguments.Trim() }
    if ($unityCommandLine) {
        # Escape spaces for Android's shell; the characters are pattern-validated.
        $launchArgs += @('--es', 'unity', (($unityCommandLine -join ' ') -replace ' ', '\ '))
        Write-Output "Unity command line: $($unityCommandLine -join ' ') (verify active threads)."
    }
    $launch = Invoke-Adb $launchArgs 60000
    if ($launch.Code -ne 0 -or $launch.Text -match 'Error:') { throw "Game launch failed: $($launch.Text) $($launch.Error)" }
    $gameStarted = $true
    if ($CollectGuestCpu) {
        # Optional diagnostics must not fail the game launch. Bind the sampler
        # to the host so it also exits if the PowerShell launcher disappears.
        try {
            $collector = Join-Path $AxrbRoot 'modules/performance_overlay/collect_guest_cpu.py'
            if (!(Test-Path -LiteralPath $collector)) { throw 'Performance collector is not included in this build.' }
            $python = (Get-Command python -ErrorAction Stop).Source
            $collectorArgs = @($collector, '--adb', $adb, '--adb-port', '5038', '--serial', $serial,
                '--package', $Package, '--parent', [string]$bridgeProcess.Id, '--output', $guestCpuFile)
            $quotedArgs = @($collectorArgs | ForEach-Object {
                '"' + [regex]::Replace([regex]::Replace($_, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
            })
            $guestCpuProcess = Start-Process -FilePath $python -ArgumentList $quotedArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs/guest-cpu.log" -RedirectStandardError "$logs/guest-cpu.err"
        } catch { Write-Output "Warning: performance CPU collector unavailable: $_" }
    }
    Write-Output "$GameName | AXRB is running. Closing its window stops this game session."
    $missing = 0
    $gamePid = $null
    $adbFailingSince = $null
    $lastAdbRecovery = $null
    $logBudgetNextCheck=[DateTime]::MinValue
    $logBudgetStopped=$false
    while (!$bridgeProcess.HasExited) {
        if($env:AXRB_SESSION_LOG_LIMIT_MB -and [DateTime]::UtcNow -ge $logBudgetNextCheck){
            $logBudgetNextCheck=[DateTime]::UtcNow.AddSeconds(10)
            $limit=[long]$env:AXRB_SESSION_LOG_LIMIT_MB*1MB
            $bytes=(Get-ChildItem -LiteralPath $AxrbOut -File -Recurse -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
            $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($AxrbOut))
            if(($limit -gt 0 -and $bytes -ge $limit) -or $drive.AvailableFreeSpace -lt 60GB){
                Write-Output 'Diagnostic disk guard: saving and ending this session at its log budget or 60GiB free-space reserve. Existing evidence is retained.'
                $logBudgetStopped=$true;$closeRequested=$true;break
            }
        }
        if ($closeRequest.WaitOne(0)) { $closeRequested = $true; break }
        $game = $null
        try { $game = Invoke-Adb @('shell', 'pidof', $Package) } catch { $adbError = $_.Exception.Message }
        if ($game -and $game.Code -eq 0 -and $game.Text) {
            $missing = 0; $adbFailingSince = $null
            $gamePid = ($game.Text -split '\s+')[0]
        } elseif ($game -and $game.Code -eq 1 -and !$game.Text -and !$game.Error) {
            # pidof ran and found nothing. Other exit codes, including an ADB
            # client crash with empty stderr, are transport failures.
            $missing++; $adbFailingSince = $null
        } else {
            # ADB is monitoring the game, not carrying its image stream. A
            # server crash must not immediately close a game still rendering.
            if ($game) { $adbError = $game.Error }
            if (!$adbFailingSince) { $adbFailingSince = Get-Date }
            $outage = ((Get-Date) - $adbFailingSince).TotalSeconds
            if ($outage -ge 5 -and (!$lastAdbRecovery -or ((Get-Date) - $lastAdbRecovery).TotalSeconds -ge 15)) {
                $lastAdbRecovery = Get-Date
                Write-Output 'Android connection interrupted; restarting ADB and reconnecting offline devices.'
                try { $null = Invoke-Adb @('start-server') 15000 } catch { }
                try { $null = Invoke-Adb @('reconnect', 'offline') 10000 } catch { }
            }
            if ($outage -ge 90) { $androidLost = $true; $gameLost = $true; break }
        }
        if ($missing -ge 2) { $gameLost = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if ($gameLost -and !$androidLost) {
        $gameExit = Get-GameExit $gamePid $knownExits
        Write-Output "Game process ended on Android: $($gameExit.kind)$(if ($gameExit.reason) { " ($($gameExit.reason), status $($gameExit.status))" })."
        if ($gameExit.kind -eq 'exited') { $gameLost = $false }
    } elseif ($androidLost) {
        $gameExit = [ordered]@{ kind = 'unreachable'; reason = "adb: $adbError"; status = $null }
        Write-Output "Warning: lost contact with Android for 90 seconds ($adbError)."
    }
    # Closing the host window sets the close event first, so a host that exits
    # without one ended on its own. Checked here, before cleanup closes it.
    # The loop may have seen the exit before the event, so ask the event again.
    if (!$closeRequested -and $closeRequest.WaitOne(0)) { $closeRequested = $true }
    if (!$closeRequested -and !$gameLost -and $bridgeProcess.HasExited) { $hostLost = $true }
} finally {
    if ($gameStarted) {
        # Give the activity its normal onPause/onStop callbacks while rendering
        # and pose transport are still alive. sync alone cannot flush app memory.
        try {
            $pause = Invoke-Adb @('shell', 'am', 'start', '-W', '-a', 'android.intent.action.MAIN', '-c', 'android.intent.category.HOME') 10000
            if ($pause.Code -ne 0 -or $pause.Text -match 'Error:') { throw "Android pause failed: $($pause.Text) $($pause.Error)" }
            $pauseSucceeded = $true
            Start-Sleep -Seconds 2
            $flush = Invoke-Adb @('shell', 'sync')
            if ($flush.Code -ne 0) { throw "Android sync failed: $($flush.Error)" }
            $syncSucceeded = $true
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
    if ($guestCpuProcess) {
        if (!$guestCpuProcess.HasExited) { $guestCpuProcess.Kill(); $guestCpuProcess.WaitForExit(3000) | Out-Null }
        $guestCpuProcess.Dispose()
    }
    if ($guestLogProcess) {
        if (!$guestLogProcess.HasExited) { $guestLogProcess.Kill(); $guestLogProcess.WaitForExit(3000) | Out-Null }
        $guestLogProcess.Dispose()
    }
    if ($bridgeProcess) { $bridgeProcess.Dispose() }
    if ($closeRequest) { $closeRequest.Dispose() }
    if ($closeReady) { $closeReady.Dispose() }
    if ($fpsHudEvent) { $fpsHudEvent.Dispose() }
    # Structured session record: the launcher reads this instead of parsing the
    # transcript above, and the diagnostics bundle ships it verbatim.
    $session = [ordered]@{
        id = $SessionId
        package = $Package; activity = $Activity; game = $GameName
        startedAt = if ($sessionStartedAt) { $sessionStartedAt.ToString('o') } else { $null }
        endedAt = (Get-Date).ToString('o')
        closeRequested = [bool]$closeRequested
        diagnosticDiskGuard = [bool]$logBudgetStopped
        gameProcessLost = [bool]($gameLost -and !$closeRequested)
        gameExit = $gameExit
        hostProcessLost = [bool]$hostLost
        pauseSucceeded = [bool]$pauseSucceeded
        syncSucceeded = [bool]$syncSucceeded
    }
    try { $session | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logs 'session.json') -Encoding UTF8 } catch { Write-Output "Warning: could not write session record: $_" }
    Write-Output 'AXRB game session stopped.'
}
# A lost game or host process is a failure, not a clean stop; the launcher keys
# its failure card on these exit codes. A game that quit by itself, like
# everything else, stays 0.
if ($gameLost -and !$closeRequested) { exit 3 }
if ($hostLost) { exit 4 }
