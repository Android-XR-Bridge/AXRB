param(
    [ValidateSet('Setup', 'Start', 'Verify', 'Install', 'Stop', 'Status')][string]$Action = 'Verify',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Avd = 'axrb-nvidia-api34',
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    [ValidateSet(34, 35, 36)][int]$ApiLevel = 34,
    [ValidateSet('x86_64', 'arm64-v8a')][string]$Abi = 'x86_64',
    [ValidateRange(2048, 16384)][int]$MemoryMB = 4096,
    [ValidateRange(2, 6)][int]$CpuCores = 4,
    [ValidateSet('Default', 'Tsc', 'TscCorrected')][string]$GuestClock = 'Default',
    [string]$RuntimeApk,
    [string]$AppApk,
    [switch]$GpuSharing,
    [switch]$ShowWindow,
    [switch]$ColdBoot,
    [switch]$RecoverUnresponsive
)
# See run_windows_game.ps1 for why these are set here rather than by the caller.
# That script also runs this one with &, so this one deliberately has no trap:
# trapping here would turn a failed emulator start into a message and let the
# caller continue as though Android had come up.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
. "$PSScriptRoot/../paths.ps1"
if ($AxrbPortableRoot) {
    if ($PSBoundParameters.ContainsKey('Sdk') -and [IO.Path]::GetFullPath($Sdk) -ne $env:ANDROID_HOME) {
        throw 'Portable launches must use the managed SDK inside the portable folder.'
    }
    $Sdk = $env:ANDROID_HOME
    if (!$PSBoundParameters.ContainsKey('Avd')) { $Avd = $(if ($AxrbPortableSettings.avd) { $AxrbPortableSettings.avd } else { 'axrb-managed-api36' }) }
    if (!$PSBoundParameters.ContainsKey('Port')) { $Port = $(if ($AxrbPortableSettings.port) { $AxrbPortableSettings.port } else { 5584 }) }
    if (!$PSBoundParameters.ContainsKey('ApiLevel')) { $ApiLevel = 36 }
    if (!$PSBoundParameters.ContainsKey('Abi')) { $Abi = 'arm64-v8a' }
}
. "$PSScriptRoot/gpu_validation.ps1"
$env:ANDROID_ADB_SERVER_PORT = '5038'
$env:ADB_SERVER_SOCKET = $null
# This script accepts console ports up to 5682, but adb only scans for emulator
# transports up to its own lower default. An emulator above that ceiling boots
# normally and is never discovered: the process stays alive while adb reports it
# missing forever. Widen the scan to the whole range we allow, before any adb
# call, so the server this script starts inherits it.
$env:ADB_LOCAL_TRANSPORT_MAX_PORT = '5683'
if ($Port % 2) { throw 'Emulator console port must be even.' }
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$emulator = Join-Path $Sdk 'emulator\emulator.exe'
$serial = "emulator-$Port"
if (!$RuntimeApk) { $RuntimeApk = "$AxrbAssets/android/runtime-$Abi/axrb-openxr-runtime-debug.apk" }
$image = "system-images;android-$ApiLevel;google_apis;x86_64"
$logs = Join-Path $AxrbOut 'logs/emulator'
# Store titles refuse to start unless the device advertises the headset
# features their manifests require, and a stock emulator image advertises
# none of them. PackageManager reads feature declarations only from the
# read-only partitions, once, while system_server starts, so they have to be
# written into /system rather than handed to the app.
$xrFeatures = @(
    'android.hardware.vr.headtracking',
    'android.hardware.vr.high_performance',
    'android.software.vr.mode',
    'android.software.xr.api.openxr',
    'android.software.xr.api.spatial',
    'android.hardware.xr.input.controller',
    'android.hardware.xr.input.hand_tracking',
    'android.hardware.xr.input.eye_tracking',
    'oculus.software.handtracking',
    'oculus.software.eye_tracking',
    'oculus.software.face_tracking',
    'oculus.software.body_tracking',
    'oculus.software.overlay_keyboard',
    'com.oculus.feature.PASSTHROUGH',
    'com.oculus.feature.RENDER_MODEL'
)
function Require-Path([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) { throw "Android startup diagnostic: $Description was not found at $Path" }
}
function Read-LogTail {
    $files = @("$logs\emulator.stdout.log", "$logs\emulator.stderr.log")
    ([string](($files | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object { Get-Content -LiteralPath $_ -Tail 12 -ErrorAction SilentlyContinue }) -join ' ')).Trim()
}
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
# Drive the child process directly. Windows PowerShell's Start-Process -PassThru
# returns a process whose ExitCode always reads back as $null, so an exit-code
# check against it treats every successful command as a failure.
function Invoke-ExternalWithTimeout([string]$Exe, [string[]]$Arguments, [int]$TimeoutSeconds = 30) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Exe
    $info.Arguments = (($Arguments | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($info)
    if ($null -eq $process) { throw "Could not start $Exe." }
    try {
        # Drain both pipes before waiting; a full pipe would block the child.
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill() } catch { }
            $process.WaitForExit(5000)
            throw "$Exe timed out after $TimeoutSeconds seconds."
        }
        [string]$output = $stdout.Result
        [string]$failure = $stderr.Result
        if ($process.ExitCode -ne 0) { throw (($failure + $output).Trim() + " (exit $($process.ExitCode))") }
        # Callers expect the per-line form that `& adb` produces, so line-anchored
        # matches and -join keep working on the result.
        return [string[]]@($output.TrimEnd("`r`n") -split '\r?\n')
    } finally { $process.Dispose() }
}
function Get-ManagedEmulatorProcess {
    # Only match the requested AVD on the requested ports, launched by this SDK.
    # Matching a name anywhere in a command line could catch an unrelated process.
    # Start-Process -ArgumentList quotes every array element individually
    # ("-avd" "axrb-managed-api36" ...), unlike the plain `-avd axrb-managed-api36`
    # shape these patterns were written against, so quotes are stripped before
    # matching instead of trying to model every quoting style in the pattern.
    $patternAvd = '(?i)(?:^|\s)-avd\s+' + [regex]::Escape($Avd) + '(?=\s|$)'
    $patternPorts = '(?i)(?:^|\s)-ports\s+' + [regex]::Escape("$Port,$($Port + 1)") + '(?=\s|$)'
    $sdkEmulator = [IO.Path]::GetFullPath((Join-Path $Sdk 'emulator'))
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $commandLine = $_.CommandLine -replace '"'
        $commandLine -match $patternAvd -and $commandLine -match $patternPorts -and
        $_.Name -match '^(qemu-system-x86_64-headless|emulator)\.exe$' -and
        $_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith($sdkEmulator + '\', [StringComparison]::OrdinalIgnoreCase)
    }
}
function Stop-StaleManagedEmulator([switch]$RequireCandidate) {
    $candidates = Get-ManagedEmulatorProcess
    if ($RequireCandidate -and !$candidates) { throw 'The unresponsive Android port is not owned by the requested AXRB emulator. Stop it manually before retrying.' }
    if (!$candidates) { return }
    $candidateIds = @($candidates | ForEach-Object { $_.ProcessId })
    $listeners = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
    if ($listeners.Count -and @($listeners | Where-Object { $candidateIds -notcontains $_.OwningProcess }).Count) {
        throw "Android console port $Port belongs to another process. AXRB will not stop it automatically."
    }
    if ($listeners.Count) {
        Write-Output "Android startup diagnostic: requesting graceful shutdown of $serial."
        try { Invoke-ExternalWithTimeout $adb @('-s', $serial, 'emu', 'kill') 10 | Out-Null }
        catch { Write-Output "Android startup diagnostic: graceful shutdown request failed: $($_.Exception.Message)" }
    } else {
        Write-Output "Android startup diagnostic: console port $Port is not listening; waiting for the old AXRB emulator to exit."
    }
    # Snapshot save and filesystem flush can take minutes. Never force-kill a
    # process from an earlier session: an ADB timeout does not prove it is safe.
    $deadline = (Get-Date).AddSeconds(180)
    do {
        $remaining = @($candidates | Where-Object {
            $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($_.ProcessId)" -ErrorAction SilentlyContinue
            $current -and $current.ExecutablePath -eq $_.ExecutablePath -and
                $current.CommandLine -eq $_.CommandLine -and $current.CreationDate -eq $_.CreationDate
        })
        if (!$remaining.Count) { return }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)
    throw "The old AXRB emulator did not shut down within 180 seconds. Close it manually and retry; AXRB did not force-stop it or alter its Android data."
}
# Verification runs the moment sys.boot_completed flips, while Android is still
# starting services and dexopting, so the ADB transport can stall well past a
# single timeout on a slow machine. One stall must not abandon a healthy boot.
function Invoke-Adb([string[]]$Arguments, [int]$TimeoutSeconds = 60, [int]$Attempts = 3) {
    for ($attempt = 1; ; $attempt++) {
        try { return Invoke-ExternalWithTimeout $adb $Arguments $TimeoutSeconds }
        catch {
            $failure = $_.Exception.Message
            # A protocol fault means the server went away mid-command: either a
            # second ADB build of a different version restarted the server it
            # did not start, or a client was killed while the server it forked
            # was still coming up.
            $transient = $failure -match 'protocol fault|connection reset|failed to check server version|cannot connect to daemon|daemon not running'
            if ($attempt -ge $Attempts) {
                if ($transient) {
                    throw "$failure. The ADB server on 127.0.0.1:5038 keeps restarting. Close other Android tools (Android Studio, scrcpy, phone suites) and any second AXRB window, then try again."
                }
                throw
            }
            Write-Host "Android startup diagnostic: adb $($Arguments -join ' ') failed ($failure); retry $attempt of $($Attempts - 1)."
            # Bring the server back deliberately instead of letting the next
            # command race another client into forking one.
            if ($transient) { try { Invoke-ExternalWithTimeout $adb @('start-server') 30 | Out-Null } catch { } }
            Start-Sleep -Seconds 3
        }
    }
}
function Verify-Gpu {
    Write-Output 'Android startup diagnostic: checking guest GLES renderer.'
    # sys.boot_completed flips before SurfaceFlinger will describe itself, and
    # an ADB transport that has just been reconnected can answer the first
    # dump with nothing at all. That is a guest still arriving, not a guest
    # without a renderer, so wait for the line instead of failing the setup.
    $glesDeadline = (Get-Date).AddSeconds(90)
    $gles = ''
    do {
        try { $gles = (Invoke-Adb @('-s', $serial, 'shell', 'dumpsys', 'SurfaceFlinger') 60 1 | Select-String '^GLES:') -join "`n" } catch { $gles = '' }
        if ($gles) { break }
        Start-Sleep -Seconds 3
    } while ((Get-Date) -lt $glesDeadline)
    if (!$gles) { throw 'Cannot identify guest GLES renderer. Android started but its graphics service never reported one; restart Android and try again.' }
    Write-Output 'Android startup diagnostic: checking guest Vulkan device.'
    $raw = Invoke-Adb @('-s', $serial, 'shell', 'cmd', 'gpu', 'vkjson') 90
    $vk = ($raw -join "`n") | ConvertFrom-Json
    $devices = @($vk.devices)
    Assert-AxrbGuestGpu -Gles $gles -Devices $devices
    New-Item -ItemType Directory -Force $logs | Out-Null
    $gles | Set-Content "$logs\guest-gles.txt"
    $raw | Set-Content "$logs\guest-vulkan.json"
    Write-Output $gles
    $devices | ForEach-Object { Write-Output "Vulkan: $($_.properties.deviceName) (vendor $($_.properties.vendorID), type $($_.properties.deviceType))" }
}
function Verify-Abi {
    Write-Output 'Android startup diagnostic: checking guest ABI.'
    [string]$abis = ((Invoke-Adb @('-s', $serial, 'shell', 'getprop', 'ro.product.cpu.abilist') 60) -join '')
    if ($Abi -notin $abis.Trim().Split(',')) {
        throw "Guest does not support requested ABI $Abi (advertised: $abis)."
    }
    if ($Abi -eq 'arm64-v8a') {
        # Bounded like the rest: a raw `& adb` here could hang without limit.
        [string]$bridge = ((Invoke-Adb @('-s', $serial, 'shell', 'getprop', 'ro.dalvik.vm.native.bridge') 60) -join '')
        $bridge = $bridge.Trim()
        if (!$bridge -or $bridge -eq '0') {
            throw 'ARM64 on this x86_64 AVD requires an enabled native bridge.'
        }
        Write-Output "ARM64 native bridge: $bridge; guest ABIs: $abis"
    }
}
function Get-GuestFeatures {
    return @(Invoke-Adb @('-s', $serial, 'shell', 'pm', 'list', 'features') 60 |
        ForEach-Object { ($_ -replace '^feature:', '').Trim() } | Where-Object { $_ })
}
# sys.boot_completed flips before PackageManager will answer, and a game
# launched in that window fails to resolve its own activity, so wait for the
# service that the launch actually depends on.
function Wait-BootCompleted([int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $booted = $false
    do {
        Start-Sleep -Seconds 2
        if (!$booted) {
            [string]$state = ''
            try { $state = ((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'getprop', 'sys.boot_completed') 10) -join '').Trim() } catch { }
            $booted = $state -eq '1'
        }
        if ($booted) {
            [string]$packages = ''
            try { $packages = ((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'cmd', 'package', 'list', 'packages', '-3') 15) -join '') } catch { }
            if ($packages -match 'package:') { return $true }
        }
    } while ((Get-Date) -lt $deadline)
    return $false
}
# Restarting the framework is enough to re-read the declarations: only
# system_server parses them, and a full reboot would cost another cold start.
function Install-XrFeatures {
    Write-Output 'Android startup diagnostic: declaring headset features in the guest.'
    $body = ($xrFeatures | ForEach-Object { "    <feature name=`"$_`" />" }) -join "`n"
    $xml = "<?xml version=`"1.0`" encoding=`"utf-8`"?>`n<permissions>`n$body`n</permissions>`n"
    $local = Join-Path ([IO.Path]::GetTempPath()) 'axrb-xr-features.xml'
    # Android's parser rejects a byte-order mark, which PowerShell's UTF8
    # encoding writes by default.
    [IO.File]::WriteAllText($local, $xml, [Text.UTF8Encoding]::new($false))
    try {
        Invoke-Adb @('-s', $serial, 'root') 30 | Out-Null
        Start-Sleep -Seconds 3
        Invoke-Adb @('-s', $serial, 'wait-for-device') 60 | Out-Null
        [string]$remount = ((Invoke-Adb @('-s', $serial, 'remount') 60) -join ' ')
        if ($remount -match 'not allowed|Permission denied|bootloader') {
            throw "the system partition stayed read-only ($($remount.Trim()))"
        }
        Invoke-Adb @('-s', $serial, 'push', $local, '/data/local/tmp/axrb-xr-features.xml') 60 | Out-Null
        Invoke-Adb @('-s', $serial, 'shell', 'cp', '/data/local/tmp/axrb-xr-features.xml', '/system/etc/permissions/axrb-xr-features.xml') 60 | Out-Null
        Invoke-Adb @('-s', $serial, 'shell', 'chmod', '644', '/system/etc/permissions/axrb-xr-features.xml') 30 | Out-Null
        # A file copied out of /data keeps its data label, which system_server
        # is not allowed to read.
        Invoke-Adb @('-s', $serial, 'shell', 'chcon', 'u:object_r:system_file:s0', '/system/etc/permissions/axrb-xr-features.xml') 30 | Out-Null
        Invoke-Adb @('-s', $serial, 'shell', 'rm', '-f', '/data/local/tmp/axrb-xr-features.xml') 30 | Out-Null
        Invoke-Adb @('-s', $serial, 'shell', 'stop') 60 | Out-Null
        Invoke-Adb @('-s', $serial, 'shell', 'start') 60 | Out-Null
        if (!(Wait-BootCompleted 240)) { throw 'Android did not finish restarting its framework' }
    } finally {
        Remove-Item -LiteralPath $local -Force -ErrorAction SilentlyContinue
        try { Invoke-ExternalWithTimeout $adb @('-s', $serial, 'unroot') 30 | Out-Null } catch { }
        Start-Sleep -Seconds 2
        try { Invoke-ExternalWithTimeout $adb @('-s', $serial, 'wait-for-device') 60 | Out-Null } catch { }
    }
}
# Missing features cost compatibility, not correctness, so a failure here is
# reported and the launch continues rather than taking Android down with it.
function Ensure-XrFeatures {
    try {
        $present = Get-GuestFeatures
        $missing = @($xrFeatures | Where-Object { $_ -notin $present })
        if (!$missing.Count) {
            Write-Output "Headset features: all $($xrFeatures.Count) already declared by the guest."
            return
        }
        Install-XrFeatures
        $present = Get-GuestFeatures
        $missing = @($xrFeatures | Where-Object { $_ -notin $present })
        if ($missing.Count) { throw "the guest still does not advertise: $($missing -join ', ')" }
        Write-Output "Headset features: declared $($xrFeatures.Count) in the guest."
    } catch {
        Write-Output "Android startup diagnostic: warning: could not declare headset features ($($_.Exception.Message -replace '\s+', ' ')). Games that require them will refuse to start."
    }
}
switch ($Action) {
    Setup {
        Run "$Sdk\cmdline-tools\latest\bin\sdkmanager.bat" @($image)
        $avds = & $emulator -list-avds
        if ($avds -notcontains $Avd) {
            'no' | & "$Sdk\cmdline-tools\latest\bin\avdmanager.bat" create avd --name $Avd --package $image --device pixel_2
            if ($LASTEXITCODE -ne 0) { throw 'AVD creation failed.' }
        }
    }
    Start {
        Require-Path $adb 'ADB executable'
        Require-Path $emulator 'Android emulator executable'
        $systemImage = Join-Path $Sdk "system-images\android-$ApiLevel\google_apis\x86_64\system.img"
        if ($env:ANDROID_AVD_HOME) {
            $avdConfig = Join-Path $env:ANDROID_AVD_HOME "$Avd.avd\config.ini"
            Require-Path $avdConfig 'managed AVD configuration'
            # Managed AVDs select their own image; game launches need not repeat its API level.
            [string]$configText = Get-Content -LiteralPath $avdConfig -Raw
            if ($configText -notmatch '(?m)^image\.sysdir\.1\s*=(.+)$') {
                throw "Android startup diagnostic: image.sysdir.1 is missing from $avdConfig"
            }
            $imageDirectory = $Matches[1].Trim()
            if (![IO.Path]::IsPathRooted($imageDirectory)) { $imageDirectory = Join-Path $Sdk $imageDirectory }
            $systemImage = Join-Path $imageDirectory 'system.img'
        }
        Require-Path $systemImage 'Android system image'
        Run $emulator @('-accel-check')
        $adbPort = $Port + 1
        Write-Output "Android startup diagnostic: SDK=$Sdk; AVD=$Avd; console=$Port; adb=$adbPort; server=127.0.0.1:5038; image=$systemImage"
        # Hyper-V, which AXRB requires, reserves TCP ranges at boot and they
        # differ per machine. A reserved console or adb port lets the emulator
        # start and stay alive while nothing can ever connect to it, which looks
        # exactly like a hung boot. Say so now instead of after the timeout.
        $reserved = @()
        try {
            foreach ($line in (netsh interface ipv4 show excludedportrange protocol=tcp 2>$null)) {
                if ($line -match '^\s*(\d+)\s+(\d+)') {
                    $rangeStart = [int]$Matches[1]; $rangeEnd = [int]$Matches[2]
                    foreach ($candidate in @($Port, $adbPort)) {
                        if ($candidate -ge $rangeStart -and $candidate -le $rangeEnd) { $reserved += "$candidate (in reserved range $rangeStart-$rangeEnd)" }
                    }
                }
            }
        } catch { }
        if ($reserved) {
            throw "Windows has reserved the port $($reserved -join ' and '). Hyper-V claims TCP ranges at startup, and Android cannot be reached on a reserved port. Choose a different Android port in Settings."
        }
        $drive = [IO.Path]::GetPathRoot($(if ($env:ANDROID_AVD_HOME) { $env:ANDROID_AVD_HOME } else { $Sdk }))
        if ($drive) {
            $freeGB = (Get-PSDrive -Name $drive.TrimEnd(':\') -ErrorAction SilentlyContinue).Free / 1GB
            if ($freeGB -and $freeGB -lt 8) { Write-Output ("Android startup diagnostic: warning: only {0:N1} GB is free on {1}." -f $freeGB, $drive) }
        }
        # Start the isolated local server explicitly.  Supplying only
        # ANDROID_ADB_SERVER_PORT keeps this a local daemon; setting
        # ANDROID_ADB_SERVER_ADDRESS makes adb treat it as a remote server and
        # prevents the client from starting it automatically.
        # A server left running from earlier inherited the narrower scan range,
        # so a high port would still go undiscovered. Restart it only when the
        # requested port actually needs the wider range.
        if ($adbPort -gt 5585) {
            Write-Output "Android startup diagnostic: adb port $adbPort is above the default scan range; restarting the ADB server with a wider range."
            try { Invoke-ExternalWithTimeout $adb @('kill-server') 15 | Out-Null } catch { }
        }
        # These two were the only adb calls in the launch that neither retried
        # nor tolerated failure, so one recoverable hiccup aborted everything.
        # 'protocol fault (couldn't read status): connection reset' is the
        # common one: another adb build sharing this server port restarts it,
        # killing whatever was in flight. The next attempt reconnects fine.
        Invoke-Adb @('start-server') 30 | Out-Null
        $devices = Invoke-Adb @('devices') 30
        $existingState = ''
        if ($devices -match "^$serial\s") {
            try { $existingState = [string]((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'get-state') 10) -join ''); $existingState = $existingState.Trim() } catch { }
            if ($existingState -eq 'device') {
                if (!$RecoverUnresponsive -or $Avd -ne 'axrb-managed-api36') { throw "$serial is already running; use Verify or Stop first." }
                [string]$bootState = ''
                $shellFailed = $false
                try { $bootState = ((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'getprop', 'sys.boot_completed') 8) -join '').Trim() } catch { $shellFailed = $true }
                if ($bootState -eq '1') { throw "$serial is answering Android shell commands; it will not be stopped automatically." }
                if (!$shellFailed) { throw "$serial is still booting; wait for Android before retrying." }
                Write-Output "Android startup diagnostic: $serial claims to be online but does not answer Android shell commands."
            } else {
                Write-Output "Android startup diagnostic: $serial is offline; cleaning up its stale managed emulator."
            }
        }
        # A previous AXRB launch may have registered with another ADB server
        # (for example the default 5037 daemon), so it would not appear above
        # even though it still holds this AVD.  Once a healthy instance was
        # ruled out, remove any same-AVD process before starting a replacement.
        if ($existingState -ne 'device' -or $RecoverUnresponsive) {
            Stop-StaleManagedEmulator -RequireCandidate:($existingState -eq 'device')
            try { Invoke-ExternalWithTimeout $adb @('reconnect', 'offline') 10 | Out-Null } catch { }
        }
        # Managed installations should reuse Android's quick-boot snapshot. Older
        # AXRB images were created with cold-boot settings; migrate that setting
        # in place so every launch does not rebuild Android from scratch.
        if ($Avd -eq 'axrb-managed-api36' -and $env:ANDROID_AVD_HOME) {
            $managedConfig = Join-Path $env:ANDROID_AVD_HOME "$Avd.avd\config.ini"
            if (Test-Path -LiteralPath $managedConfig) {
                [string]$configText = Get-Content -LiteralPath $managedConfig -Raw
                $configText = $configText -replace '(?m)^fastboot\.forceColdBoot=.*$', 'fastboot.forceColdBoot=no'
                $configText = $configText -replace '(?m)^fastboot\.forceFastBoot=.*$', 'fastboot.forceFastBoot=yes'
                if ($configText -notmatch '(?m)^fastboot\.forceColdBoot=') { $configText += "`nfastboot.forceColdBoot=no`n" }
                if ($configText -notmatch '(?m)^fastboot\.forceFastBoot=') { $configText += "`nfastboot.forceFastBoot=yes`n" }
                Set-Content -LiteralPath $managedConfig -Value $configText -Encoding ascii
            }
        }
        New-Item -ItemType Directory -Force $logs | Out-Null
        # Declare both ports explicitly. This avoids the emulator frontend and
        # raw QEMU clock launcher disagreeing about the ADB port.
        $arguments = @('-avd', $Avd, '-ports', "$Port,$adbPort", '-gpu', 'host', '-accel', 'on', '-no-boot-anim', '-memory', "$MemoryMB")
        if ($PSBoundParameters.ContainsKey('CpuCores')) { $arguments += @('-cores', "$CpuCores") }
        if (!$ShowWindow) { $arguments += '-no-window' }
        # Always, not once: the emulator serves /system from a scratch overlay
        # that it discards on the next launch without this flag, so a guest
        # booted read-only comes up with the headset features gone and no way
        # to put them back.
        $arguments += '-writable-system'
        if ($ColdBoot -and $GuestClock -ne 'TscCorrected') { $arguments += '-no-snapshot-load' }
        if ($GuestClock -eq 'TscCorrected') {
            # The clock-correction launcher cannot safely combine its host clock
            # shim with a persisted Android snapshot. Keep normal launches on
            # quick boot, but make the corrected-clock mode explicit and cold.
            # This has to precede -qemu: the emulator consumes its own options
            # only before that separator, and QEMU proper rejects -no-snapshot.
            $arguments += '-no-snapshot'
        }
        if ($GuestClock -ne 'Default') {
            # Request the CPU clock. tsc=reliable tells the kernel to trust the
            # WHPX-preserved TSC even after detecting minor cross-vCPU warp,
            # which eliminates the HPET fallback that accounts for ~45% of CPU
            # samples in profiling. nohpet disables the HPET interrupt entirely,
            # removing the kernel's fallback timer. no_timer_check prevents the
            # kernel from re-validating the TSC on every CPU hotplug.
            # QEMU's extra kernel options are appended to the Android defaults.
            # Keep this last; everything after -qemu goes to QEMU, not the emulator.
            $tscOptions = 'clocksource=tsc tsc=reliable no_timer_check'
            if ($GuestClock -eq 'TscCorrected') {
                # With the WHPX hook active, TSC is guaranteed stable. Be more
                # aggressive: disable HPET entirely to eliminate all HPET overhead.
                $tscOptions += ' nohpet'
            }
            $arguments += @('-show-kernel', '-qemu', '-append', $tscOptions)
        }
        $oldLayerPath = $env:VK_LAYER_PATH
        $oldLayers = $env:VK_INSTANCE_LAYERS
        $oldPath = $env:PATH
        $oldLauncherDir = $env:ANDROID_EMULATOR_LAUNCHER_DIR
        $launchExe = $emulator
        if ($GuestClock -eq 'TscCorrected') {
            $qemu = Join-Path $Sdk 'emulator/qemu/windows-x86_64/qemu-system-x86_64-headless.exe'
            $knownHash = 'DCEC1CC23AC57FF04EC748CDE7E42BFC713BF2AD532E49606A4A9332CFB94B56'
            Require-Path $qemu 'headless QEMU binary for clock correction'
            Require-Path "$AxrbClockDirectory/axrb_clock_launcher.exe" 'AXRB clock launcher'
            Require-Path "$AxrbClockDirectory/axrb_whpx_clock.dll" 'AXRB clock adapter'
            if ((Get-FileHash -LiteralPath $qemu -Algorithm SHA256).Hash -ne $knownHash) {
                throw 'Clock correction is tested only with emulator 36.5.11 build 15261927. Use -GuestClock Default for other builds.'
            }
            $launchExe = (Resolve-Path "$AxrbClockDirectory/axrb_clock_launcher.exe").Path
            $clockDll = (Resolve-Path "$AxrbClockDirectory/axrb_whpx_clock.dll").Path
            $arguments = @(('"' + $qemu + '"'), ('"' + $clockDll + '"')) + $arguments
        }
        try {
            if ($GuestClock -eq 'TscCorrected') {
                $env:ANDROID_EMULATOR_LAUNCHER_DIR = Join-Path $Sdk 'emulator'
                $env:PATH = "$Sdk\emulator;$Sdk\emulator\lib64;$oldPath"
            }
            if ($GpuSharing) {
                $layerPath = (Resolve-Path $AxrbGpuDirectory).Path
                if (!(Test-Path "$layerPath\axrb_gpu_layer.json")) { throw 'Build host/gpu first.' }
                $env:VK_LAYER_PATH = $layerPath
                $env:VK_INSTANCE_LAYERS = 'VK_LAYER_AXRB_gpu_share'
            }
            $process = Start-Process $launchExe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\emulator.stdout.log" -RedirectStandardError "$logs\emulator.stderr.log"
            if ($null -eq $process) { throw "Could not start $launchExe." }
            # Caching the handle while the process is alive is what keeps
            # ExitCode readable later; without it the property reads back $null.
            $null = $process.Handle
        } finally {
            $env:VK_LAYER_PATH = $oldLayerPath
            $env:VK_INSTANCE_LAYERS = $oldLayers
            $env:PATH = $oldPath
            $env:ANDROID_EMULATOR_LAUNCHER_DIR = $oldLauncherDir
        }
        # First boot after installing an image can take several minutes while
        # Android creates userdata and compiles system services.
        $startedAt = Get-Date
        $lastDiagnostic = $startedAt
        $adbReconnectAttempted = $false
        $devicesReported = $false
        $adbServerRestarted = $false
        # Corrected TSC mode deliberately cold-boots Android and can spend
        # several minutes unpacking and registering APEX modules on first use.
        $bootTimeoutMinutes = if ($GuestClock -eq 'TscCorrected') { 15 } else { 8 }
        $deadline = $startedAt.AddMinutes($bootTimeoutMinutes)
        $booted = $false
        do {
            Start-Sleep -Seconds 2
            $ErrorActionPreference = 'Continue'
            # ADB commonly reports `offline` while adbd is starting. Treat
            # that as a retryable state instead of aborting the whole launch.
            [string]$bootText = ''; $bootFailure = ''
            try { $bootText = (Invoke-ExternalWithTimeout $adb @('-s', $serial, 'shell', 'getprop', 'sys.boot_completed') 10) -join '' }
            catch { $bootText = ''; $bootFailure = ($_.Exception.Message -replace '\s+', ' ').Trim() }
            $ErrorActionPreference = 'Stop'
            $bootText = $bootText.Trim()
            if ($bootText -eq '1') { $booted = $true; break }
            if (((Get-Date) - $lastDiagnostic).TotalSeconds -ge 30) {
                # Never collapse "adb could not reach this serial" into the same
                # word as "adb reports the transport offline": they are different
                # faults and reading 'offline' for both hid which one occurred.
                [string]$adbText = ''
                $adbOffline = $false
                try {
                    $adbText = ((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'get-state') 10) -join '').Trim()
                    $adbOffline = $adbText -eq 'offline'
                } catch {
                    $failure = ($_.Exception.Message -replace '\s+', ' ').Trim()
                    $adbOffline = $failure -match '\bdevice offline\b'
                    $adbText = if ($adbOffline) { "offline ($failure)" } else { "unreachable: $failure" }
                }
                if (!$adbText) { $adbText = 'no answer' }
                if ($bootFailure -and $bootText -eq '') { $bootText = "(getprop failed: $bootFailure)" }
                Write-Output ("Android startup diagnostic: {0}s elapsed; adb={1}; boot={2}; processExited={3}" -f [int]((Get-Date) - $startedAt).TotalSeconds, $adbText, $bootText, $process.HasExited)
                # One listing settles whether adb sees nothing at all, sees this
                # transport but offline, or sees a different serial entirely.
                if (!$devicesReported) {
                    $devicesReported = $true
                    $seen = 'none'
                    try { $seen = ((Invoke-ExternalWithTimeout $adb @('devices') 10) | Where-Object { $_ -match '\S' -and $_ -notmatch '^List of devices' }) -join '; ' } catch { $seen = "listing failed: $($_.Exception.Message)" }
                    if (!$seen) { $seen = 'none' }
                    Write-Output "Android startup diagnostic: expecting $serial; adb currently lists: $seen"
                }
                if ($adbOffline) {
                    if (!$adbReconnectAttempted) {
                        Write-Output 'Android startup diagnostic: reconnecting offline ADB transport.'
                        try { Invoke-ExternalWithTimeout $adb @('reconnect', 'offline') 10 | Out-Null } catch { }
                        $adbReconnectAttempted = $true
                    } elseif (!$adbServerRestarted -and ((Get-Date) - $startedAt).TotalSeconds -ge 90) {
                        Write-Output 'Android startup diagnostic: restarting ADB server after persistent offline transport.'
                        try { Invoke-ExternalWithTimeout $adb @('kill-server') 15 | Out-Null } catch { }
                        try { Invoke-ExternalWithTimeout $adb @('start-server') 15 | Out-Null } catch { }
                        $adbServerRestarted = $true
                    }
                }
                $lastDiagnostic = Get-Date
            }
            if ($process.HasExited) {
                $process.Refresh()
                $exitCode = try { [string]$process.ExitCode } catch { 'unknown' }
                $detail = Read-LogTail
                throw "Emulator exited ($exitCode). Recent emulator output: $detail Logs: $logs\emulator.stdout.log and $logs\emulator.stderr.log"
            }
        } while ((Get-Date) -lt $deadline)
        if (!$booted) { throw "Android did not finish booting within $bootTimeoutMinutes minutes. Last emulator output: $(Read-LogTail) Logs: $logs\emulator.stdout.log and $logs\emulator.stderr.log" }
        try { Verify-Gpu; Verify-Abi; Ensure-XrFeatures; Write-Output 'Android startup diagnostic: guest verification complete.' } catch {
            try { Invoke-ExternalWithTimeout $adb @('-s', $serial, 'emu', 'kill') 5 | Out-Null } catch { }
            throw
        }
        if ($GuestClock -ne 'Default') {
            # Verify the kernel accepted TSC. If it fell back to HPET, attempt
            # runtime enforcement: force TSC via sysfs and disable HPET interrupts.
            [string]$clock = (& $adb -s $serial shell su 0 cat /sys/devices/system/clocksource/clocksource0/current_clocksource) -join ''
            if ($clock.Trim() -eq 'tsc') {
                Write-Output 'Guest clock: TSC (accepted by Linux stability checks).'
            } else {
                Write-Output "Android startup diagnostic: guest retained '$($clock.Trim())'; attempting runtime TSC enforcement."
                # Force TSC via sysfs (requires root)
                & $adb -s $serial shell su 0 'sh -c "echo tsc > /sys/devices/system/clocksource/clocksource0/current_clocksource"' 2>$null
                Start-Sleep -Milliseconds 200
                [string]$clockAfter = (& $adb -s $serial shell su 0 cat /sys/devices/system/clocksource/clocksource0/current_clocksource) -join ''
                if ($clockAfter.Trim() -eq 'tsc') {
                    Write-Output 'Guest clock: TSC (enforced via sysfs runtime override).'
                } else {
                    Write-Output "Android startup diagnostic: warning: TSC enforcement failed; guest uses '$($clockAfter.Trim())'. The WHPX clock hook may not be active."
                }
            }
            # Measure clock overhead to quantify the optimization impact
            try {
                [string]$clockLatency = (& $adb -s $serial shell su 0 'cat /proc/uptime' 2>$null) -join ''
                if ($clockLatency) { Write-Output "Guest clock: uptime=$clockLatency" }
            } catch { }
        }
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490') | Out-Null
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491') | Out-Null
        Run $adb @('-s', $serial, 'shell', 'setprop', 'debug.axrb.gpu_share', $(if ($GpuSharing) { '1' } else { '0' })) | Out-Null
        Write-Output "Ready: $serial. Images use adb reverse :38491; native pose stream uses 10.0.2.2:38490."
    }
    Verify { Verify-Gpu; Verify-Abi }
    Status {
        # A lightweight, side-effect-free check for the watchdog: never starts,
        # stops, or reconnects anything, and never throws for a not-yet-set-up
        # SDK or an unreachable device — those are normal, frequently-polled states.
        $candidates = @(Get-ManagedEmulatorProcess)
        $adbState = ''
        if (Test-Path -LiteralPath $adb) {
            try { $adbState = ((Invoke-ExternalWithTimeout $adb @('-s', $serial, 'get-state') 5) -join '').Trim() }
            catch { $adbState = '' }
        }
        # Windows PowerShell's ConvertTo-Json renders the pipeline's "Nothing"
        # result (what -ExpandProperty on an empty collection produces) as {}
        # rather than null, so pick out the value with a plain index instead.
        $processId = if ($candidates.Count) { $candidates[0].ProcessId } else { $null }
        [pscustomobject]@{
            running = [bool]$candidates.Count
            processId = $processId
            count = $candidates.Count
            adbState = $adbState
        } | ConvertTo-Json -Compress
    }
    Install {
        Verify-Gpu
        Verify-Abi
        Run $adb @('-s', $serial, 'install', '--no-incremental', '--force-queryable', '-r', $RuntimeApk)
        Run $adb @('-s', $serial, 'reverse', 'tcp:38490', 'tcp:38490') | Out-Null
        Run $adb @('-s', $serial, 'reverse', 'tcp:38491', 'tcp:38491') | Out-Null
        if ($AppApk) { Run $adb @('-s', $serial, 'install', '--no-incremental', '-r', $AppApk) }
    }
    Stop { Run $adb @('-s', $serial, 'emu', 'kill') }
}
