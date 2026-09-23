<#
.SYNOPSIS
Samples a running AXRB session and prints a report that explains where the
frame time is going.

.DESCRIPTION
A frame rate complaint is almost never answerable from a log, because the logs
record what happened and not what was busy. This samples the four places the
work can be sitting while a game runs -- the Windows processes, the GPU
engines, the guest's threads, and the bridge's own timers -- over one window,
so the numbers can be compared against each other.

Everything here is read-only and the report is de-identified before it is
printed, so it is safe to hand to someone else for help.

.EXAMPLE
.\performance_scan.ps1 -OutFile "$env:USERPROFILE\Desktop\axrb-scan.txt"
Runs a ten-second scan while a game is playing and writes the report to a file.
#>
param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [ValidateRange(5554, 5682)][int]$Port = 5580,
    # Long enough to average out a slow frame, short enough that nobody takes
    # the headset off waiting for it.
    [ValidateRange(3, 60)][int]$Seconds = 10,
    [ValidatePattern('^$|^[a-zA-Z0-9_.]+$')][string]$Package = '',
    [string]$Version = '',
    [string]$OutFile = ''
)
# See run_windows_game.ps1 for why these are set here rather than by the caller.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
# Reports get compared with each other and with the ones in the docs, so the
# numbers are formatted the same everywhere: on a German Windows the default
# culture would write 22,127 for twenty-two milliseconds.
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture
. "$PSScriptRoot/../paths.ps1"
# The launcher runs its own ADB daemon on this port; talking to the default one
# would start a second server and find no device.
$env:ANDROID_ADB_SERVER_PORT = '5038'

$report = [System.Collections.Generic.List[string]]::new()
function Emit([string]$Text = '') { [void]$report.Add($Text) }

# The report is written to be shared, so the three details a Windows path
# always leaks are replaced. The home directory goes first because it contains
# the account name, and replacing the name alone would leave a broken path;
# both separators appear, since Node and the Android tooling print the same
# path with forward slashes. Standalone words only match whole, so a machine
# called "pc" does not shred the rest of the report.
function Protect-Text([string]$Text) {
    if (!$Text) { return '' }
    if ($HOME) { $Text = [regex]::Replace($Text, (([regex]::Escape($HOME)) -replace '\\\\', '[\\/]'), '<home>', 'IgnoreCase') }
    foreach ($pair in @(, @($env:USERNAME, '<user>')) + @(, @($env:COMPUTERNAME, '<computer>'))) {
        if (!$pair[0]) { continue }
        $Text = [regex]::Replace($Text, "(?<![A-Za-z0-9_-])$([regex]::Escape($pair[0]))(?![A-Za-z0-9_-])", $pair[1], 'IgnoreCase')
    }
    return $Text
}

# ---------------------------------------------------------------- guest access
$adb = Join-Path $Sdk 'platform-tools\adb.exe'
$serial = "emulator-$Port"
# Every guest read is bounded: a guest that is wedged is exactly the case this
# script is meant to describe, so it must not hang on one.
function Invoke-Adb([string[]]$Arguments, [int]$TimeoutMs = 15000) {
    if (!(Test-Path -LiteralPath $adb)) { return @{ Code = -1; Text = ''; Error = 'adb not found' } }
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $adb
    $info.Arguments = (@('-s', $serial) + $Arguments) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $child = [System.Diagnostics.Process]::Start($info)
    if ($null -eq $child) { return @{ Code = -1; Text = ''; Error = 'adb could not be started' } }
    try {
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        if (!$child.WaitForExit($TimeoutMs)) { $child.Kill(); return @{ Code = -1; Text = ''; Error = 'adb timed out' } }
        return @{ Code = $child.ExitCode; Text = ([string]$stdout.Result).Replace("`r", ''); Error = ([string]$stderr.Result).Trim() }
    } finally { $child.Dispose() }
}
function Invoke-GuestShell([string]$Command, [int]$TimeoutMs = 15000) {
    $result = Invoke-Adb @('shell', $Command) $TimeoutMs
    if ($result.Code -ne 0) { return '' }
    return $result.Text
}

# ------------------------------------------------------------ host measurement
# TotalProcessorTime is cumulative, so two reads a known distance apart give the
# share of one core each process used during the window. Protected processes
# refuse the read; they are not ours and are skipped.
function Get-ProcessSnapshot {
    $snapshot = @{}
    foreach ($process in Get-Process -ErrorAction SilentlyContinue) {
        try { $cpu = $process.TotalProcessorTime.TotalMilliseconds } catch { continue }
        $snapshot[$process.Id] = @{ Name = $process.ProcessName; Cpu = $cpu; Memory = $process.PrivateMemorySize64 }
    }
    return $snapshot
}

# Performance counter paths are localized, so the English literal fails on a
# German or Japanese Windows. The name database under Perflib maps every
# counter to a numeric index in 009 (English) and back out in the display
# language, which turns the English name into whatever this machine calls it.
$script:counterNames = $null
function Resolve-CounterName([string]$English) {
    if ($null -eq $script:counterNames) {
        $script:counterNames = @{}
        try {
            $base = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib'
            # PowerShell variables are case-insensitive, so neither of these may
            # be called $english: that would overwrite the parameter.
            $englishNames = (Get-ItemProperty -LiteralPath "$base\009" -Name Counter).Counter
            $localizedNames = (Get-ItemProperty -LiteralPath "$base\CurrentLanguage" -Name Counter).Counter
            $byIndex = @{}
            for ($i = 0; $i + 1 -lt $localizedNames.Count; $i += 2) { $byIndex[$localizedNames[$i]] = $localizedNames[$i + 1] }
            for ($i = 0; $i + 1 -lt $englishNames.Count; $i += 2) {
                $name = $englishNames[$i + 1]
                if ($name -and $byIndex.ContainsKey($englishNames[$i])) { $script:counterNames[$name] = $byIndex[$englishNames[$i]] }
            }
        } catch { }
    }
    if ($script:counterNames.ContainsKey($English)) { return $script:counterNames[$English] }
    return $English
}

# Task Manager's single "GPU %" is the busiest engine, not the sum of them, so
# the engines are reported apart: a title that is copy-bound and one that is
# shader-bound look identical once they are added together.
function Measure-Gpu([int]$Samples = 3) {
    $path = '\{0}(*)\{1}' -f (Resolve-CounterName 'GPU Engine'), (Resolve-CounterName 'Utilization Percentage')
    try { $counters = Get-Counter -Counter $path -SampleInterval 1 -MaxSamples $Samples -ErrorAction Stop }
    catch { return $null }
    $byEngine = @{}
    $byProcess = @{}
    foreach ($set in $counters) {
        foreach ($value in $set.CounterSamples) {
            if ($value.CookedValue -le 0) { continue }
            $engine = 'other'
            if ($value.InstanceName -match 'engtype_(\w+)') { $engine = $Matches[1] }
            $owner = 'unknown'
            if ($value.InstanceName -match '^pid_(\d+)_') { $owner = $Matches[1] }
            $byEngine[$engine] = [double]$byEngine[$engine] + $value.CookedValue
            $byProcess[$owner] = [double]$byProcess[$owner] + $value.CookedValue
        }
    }
    foreach ($key in @($byEngine.Keys)) { $byEngine[$key] = $byEngine[$key] / $counters.Count }
    foreach ($key in @($byProcess.Keys)) { $byProcess[$key] = $byProcess[$key] / $counters.Count }
    return @{ Engines = $byEngine; Processes = $byProcess }
}

# ------------------------------------------------------------------- inventory
Emit ("AXRB performance scan {0}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
$osInfo = Get-CimInstance Win32_OperatingSystem
Emit ("launcher {0}; Windows {1}; window {2}s" -f $(if ($Version) { $Version } else { 'unknown' }), $osInfo.Version, $Seconds)
foreach ($cpu in @(Get-CimInstance Win32_Processor)) {
    Emit ("cpu     {0} -- {1} cores / {2} threads" -f $cpu.Name.Trim(), $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors)
}
$logical = [int]$env:NUMBER_OF_PROCESSORS
Emit ("memory  {0:N1} GB total, {1:N1} GB free" -f ($osInfo.TotalVisibleMemorySize / 1MB), ($osInfo.FreePhysicalMemory / 1MB))
foreach ($adapter in @(Get-CimInstance Win32_VideoController)) {
    Emit ("gpu     {0} -- driver {1}" -f $adapter.Name, $adapter.DriverVersion)
}
# Which OpenXR runtime is active decides whether the headset path is SteamVR,
# Oculus, or something else, and a mismatched one is a common cause of a
# session that runs but runs badly.
try {
    $active = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction Stop).ActiveRuntime
    Emit ("openxr  {0}" -f (Protect-Text $active))
} catch { Emit 'openxr  (no active runtime registered)' }

# SteamVR's own supersampling multiplies every pixel the guest has to produce,
# and a manual override here outweighs anything AXRB can do about frame time.
$steamvr = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
if (Test-Path -LiteralPath $steamvr) {
    try {
        $paths = Get-Content -LiteralPath $steamvr -Raw | ConvertFrom-Json
        foreach ($configDirectory in @($paths.config)) {
            $settingsFile = Join-Path $configDirectory 'steamvr.vrsettings'
            if (!(Test-Path -LiteralPath $settingsFile)) { continue }
            $settings = Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json
            $interesting = @()
            foreach ($key in @('supersampleScale', 'allowSupersampleFiltering', 'allowSupersampleScaling', 'motionSmoothing', 'motionSmoothingOverride', 'forcedFadeoutDistance', 'maxRecommendedResolution')) {
                if (!$settings.steamvr -or ($null -eq $settings.steamvr.$key)) { continue }
                $value = $settings.steamvr.$key
                # A slider writes 1.4999999999999998; that is noise, not detail.
                if ($value -is [ValueType] -and $value -isnot [bool]) { $value = [Math]::Round([double]$value, 4) }
                $interesting += "$key=$value"
            }
            if ($interesting.Count) { Emit ("steamvr {0}" -f ($interesting -join '  ')) }
            else { Emit 'steamvr no render overrides set (auto resolution)' }
        }
    } catch { Emit 'steamvr (settings unreadable)' }
}

# ------------------------------------------------------------- the sample window
$gpu = Measure-Gpu
$gamePid = ''
if (!$Package) {
    # The resumed activity is the game: the AXRB runtime is a library inside it
    # and never owns the foreground itself.
    $focus = Invoke-GuestShell "dumpsys activity activities | grep -m2 -E 'topResumedActivity|mResumedActivity'"
    # A game is a third-party package. Anything under com.android or com.google
    # in the foreground means no game is playing, and profiling the system
    # launcher would produce a page of zeroes that looks like an answer.
    if ($focus -match '([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+)/' -and $Matches[1] -notmatch '^com\.(android|google)\.') { $Package = $Matches[1] }
}
if ($Package) { $gamePid = (Invoke-GuestShell "pidof $Package").Trim().Split(' ')[0] }

# One shell per snapshot rather than one per file: the guest side of an emulated
# ADB round trip costs more than the reads do, and the two snapshots have to
# bracket the same window on the host.
$guestCommand = "echo ===clk; getconf CLK_TCK; echo ===uptime; cat /proc/uptime; echo ===stat; grep -a '^cpu' /proc/stat; echo ===mem; grep -aE 'MemTotal|MemAvailable' /proc/meminfo"
# -H matters: with a single-threaded process the glob expands to one file and
# grep would then drop the path prefix the thread id is read from.
if ($gamePid) { $guestCommand += "; echo ===comm; grep -aH . /proc/$gamePid/task/*/comm; echo ===tstat; grep -aH . /proc/$gamePid/task/*/stat; echo ===ctxt; grep -aH ctxt_switches /proc/$gamePid/task/*/status" }

function Split-GuestSnapshot([string]$Text) {
    $sections = @{}
    $current = ''
    foreach ($line in $Text.Split("`n")) {
        if ($line.StartsWith('===')) { $current = $line.Substring(3).Trim(); $sections[$current] = [System.Collections.Generic.List[string]]::new(); continue }
        if ($current -and $line) { [void]$sections[$current].Add($line) }
    }
    return $sections
}
# /proc/<pid>/task/<tid>/stat: comm sits in parentheses and may contain spaces
# or a ')' of its own, so the fields are counted from the last ') ' onwards.
function Get-TaskTimes($Lines) {
    $times = @{}
    foreach ($line in $Lines) {
        if ($line -notmatch '^/proc/\d+/task/(\d+)/stat:') { continue }
        $tid = $Matches[1]
        $cut = $line.LastIndexOf(') ')
        if ($cut -lt 0) { continue }
        $fields = $line.Substring($cut + 2).Split(' ')
        if ($fields.Count -lt 13) { continue }
        $times[$tid] = @{ User = [double]$fields[11]; System = [double]$fields[12] }
    }
    return $times
}
function Get-TaskSwitches($Lines) {
    $switches = @{}
    foreach ($line in $Lines) {
        if ($line -notmatch '^/proc/\d+/task/(\d+)/status:(non)?voluntary_ctxt_switches:\s*(\d+)') { continue }
        $tid = $Matches[1]
        if (!$switches.ContainsKey($tid)) { $switches[$tid] = @{ Voluntary = 0.0; Involuntary = 0.0 } }
        if ($Matches[2]) { $switches[$tid].Involuntary = [double]$Matches[3] } else { $switches[$tid].Voluntary = [double]$Matches[3] }
    }
    return $switches
}
function Get-CpuBusy($Lines) {
    # /proc/stat's aggregate line, minus the two columns that are not work.
    foreach ($line in $Lines) {
        if ($line -notmatch '^cpu\s') { continue }
        $fields = ($line -split '\s+') | Where-Object { $_ -match '^\d+$' }
        $total = 0.0; foreach ($field in $fields) { $total += [double]$field }
        return @{ Total = $total; Idle = [double]$fields[3] + [double]$fields[4] }
    }
    return $null
}

$guestBefore = Split-GuestSnapshot (Invoke-GuestShell $guestCommand 30000)
$hostBefore = Get-ProcessSnapshot
$startedAt = [DateTime]::UtcNow
Start-Sleep -Seconds $Seconds
$hostAfter = Get-ProcessSnapshot
$elapsedMs = ([DateTime]::UtcNow - $startedAt).TotalMilliseconds
$guestAfter = Split-GuestSnapshot (Invoke-GuestShell $guestCommand 30000)

# --------------------------------------------------------------- host results
Emit ''
Emit ("--- windows processes ({0}s window, % of one core; {1} logical cores available) ---" -f [int]($elapsedMs / 1000), $logical)
$rows = @()
$hostTotal = 0.0
foreach ($id in $hostAfter.Keys) {
    if (!$hostBefore.ContainsKey($id)) { continue }
    $percent = ($hostAfter[$id].Cpu - $hostBefore[$id].Cpu) / $elapsedMs * 100
    $hostTotal += $percent
    $rows += [pscustomobject]@{ Name = $hostAfter[$id].Name; Percent = $percent; Memory = $hostAfter[$id].Memory / 1MB }
}
# These four decide whether a session is healthy, so they are listed even when
# idle: a missing bridge or a pegged vrserver is itself the answer.
# QEMU runs as its multi-core copy on CPUs whose CPUID the emulator distrusts.
$qemuName = @($rows.Name | Where-Object { $_ -like 'qemu-system-x86_64*' } | Select-Object -First 1)
$always = @($(if ($qemuName) { $qemuName[0] } else { 'qemu-system-x86_64-headless' }), 'axrb-host-bridge', 'vrserver', 'vrcompositor')
$shown = @($rows | Sort-Object Percent -Descending | Select-Object -First 12)
$missing = @()
foreach ($name in $always) {
    if ($shown.Name -notcontains $name) { $shown += @($rows | Where-Object Name -eq $name) }
    if ($shown.Name -notcontains $name) { $missing += $name }
}
foreach ($row in ($shown | Sort-Object Percent -Descending)) {
    Emit ("  {0,-34} {1,7:N1}%  {2,8:N0} MB" -f $row.Name, $row.Percent, $row.Memory)
}
Emit ("  {0,-34} {1,7:N1}%  ({2:N0}% of all cores)" -f 'TOTAL', $hostTotal, ($hostTotal / [Math]::Max(1, $logical)))
foreach ($name in $missing) { Emit ("  {0,-34} not running" -f $name) }

Emit ''
if ($gpu) {
    Emit '--- gpu engines (% busy, averaged over 3s) ---'
    foreach ($engine in ($gpu.Engines.GetEnumerator() | Sort-Object Value -Descending)) {
        Emit ("  {0,-34} {1,7:N1}%" -f $engine.Key, $engine.Value)
    }
    $owners = @()
    foreach ($owner in ($gpu.Processes.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 5)) {
        $process = Get-Process -Id ([int]$owner.Key) -ErrorAction SilentlyContinue
        $label = $(if ($process) { $process.ProcessName } else { "pid $($owner.Key)" })
        $owners += ("{0} {1:N1}%" -f $label, $owner.Value)
    }
    if ($owners.Count) { Emit ("  by process: {0}" -f ($owners -join ', ')) }
} else { Emit '--- gpu engines --- (counters unavailable on this machine)' }
# Vendor tooling adds the two numbers Windows will not report: how hard the
# board is actually being driven, and whether it is clocking down.
$nvidiaSmi = Join-Path $env:SystemRoot 'System32\nvidia-smi.exe'
if (Test-Path -LiteralPath $nvidiaSmi) {
    $query = & $nvidiaSmi '--query-gpu=utilization.gpu,power.draw,power.limit,clocks.sm,temperature.gpu' '--format=csv,noheader'
    foreach ($line in @($query)) { if ($line) { Emit ("  nvidia-smi: {0}" -f ([string]$line).Trim()) } }
}

# -------------------------------------------------------------- guest results
Emit ''
$busyBefore = Get-CpuBusy $guestBefore['stat']
$busyAfter = Get-CpuBusy $guestAfter['stat']
if (!$busyAfter) {
    Emit '--- android guest --- (not reachable; is the emulator running on this port?)'
} else {
    $vcpus = @($guestAfter['stat'] | Where-Object { $_ -match '^cpu\d' }).Count
    $ticks = 100
    if ($guestAfter['clk'] -and $guestAfter['clk'][0] -match '^\d+$') { $ticks = [int]$guestAfter['clk'][0] }
    $guestSeconds = [double]($guestAfter['uptime'][0] -split '\s+')[0] - [double]($guestBefore['uptime'][0] -split '\s+')[0]
    if ($guestSeconds -le 0) { $guestSeconds = $elapsedMs / 1000 }
    $busy = 0.0
    if ($busyBefore) {
        $span = $busyAfter.Total - $busyBefore.Total
        if ($span -gt 0) { $busy = (1 - ($busyAfter.Idle - $busyBefore.Idle) / $span) * 100 * $vcpus }
    }
    $memory = ($guestAfter['mem'] -join '  ') -replace '\s+', ' '
    Emit ("--- android guest ({0} vCPUs, {1:N1}s of guest time) ---" -f $vcpus, $guestSeconds)
    Emit ("  cpu busy {0:N1}% of {1}00% available    {2}" -f $busy, $vcpus, $memory)
    if (!$gamePid) {
        Emit '  no game is in the foreground. Start one, or name it with -Package.'
    } else {
        Emit ("  {0} (pid {1})" -f $Package, $gamePid)
        Emit ("  {0,-24} {1,6} {2,6} {3,7} {4,8} {5,8}" -f 'thread', 'user%', 'sys%', 'total%', 'vol/s', 'invol/s')
        $names = @{}
        foreach ($line in $guestAfter['comm']) { if ($line -match '^/proc/\d+/task/(\d+)/comm:(.*)$') { $names[$Matches[1]] = $Matches[2] } }
        $before = Get-TaskTimes $guestBefore['tstat']
        $after = Get-TaskTimes $guestAfter['tstat']
        $switchesBefore = Get-TaskSwitches $guestBefore['ctxt']
        $switchesAfter = Get-TaskSwitches $guestAfter['ctxt']
        $threads = @()
        $guestUser = 0.0; $guestSystem = 0.0
        foreach ($tid in $after.Keys) {
            if (!$before.ContainsKey($tid)) { continue }
            $user = ($after[$tid].User - $before[$tid].User) / $ticks / $guestSeconds * 100
            $system = ($after[$tid].System - $before[$tid].System) / $ticks / $guestSeconds * 100
            $guestUser += $user; $guestSystem += $system
            if ($user + $system -lt 0.5) { continue }
            $voluntary = 0.0; $involuntary = 0.0
            if ($switchesAfter.ContainsKey($tid) -and $switchesBefore.ContainsKey($tid)) {
                $voluntary = ($switchesAfter[$tid].Voluntary - $switchesBefore[$tid].Voluntary) / $guestSeconds
                $involuntary = ($switchesAfter[$tid].Involuntary - $switchesBefore[$tid].Involuntary) / $guestSeconds
            }
            $threads += [pscustomobject]@{ Label = "$($names[$tid])($tid)"; User = $user; System = $system; Total = $user + $system; Voluntary = $voluntary; Involuntary = $involuntary }
        }
        $threads = @($threads | Sort-Object Total -Descending)
        foreach ($thread in ($threads | Select-Object -First 20)) {
            Emit ("  {0,-24} {1,6:N1} {2,6:N1} {3,7:N1} {4,8:N0} {5,8:N0}" -f $thread.Label, $thread.User, $thread.System, $thread.Total, $thread.Voluntary, $thread.Involuntary)
        }
        Emit ("  {0,-24} {1,6:N1} {2,6:N1} {3,7:N1}" -f 'TOTAL', $guestUser, $guestSystem, ($guestUser + $guestSystem))
        $script:topThread = $(if ($threads.Count) { $threads[0] } else { $null })
        $script:guestBusy = $guestUser + $guestSystem
        $script:vcpus = $vcpus
    }
}

# ------------------------------------------------------------- bridge timings
# Both sides publish the same five-second summaries: the host writes them to
# host.err, the guest to logcat. Only the most recent window of each counter is
# kept, because the earlier ones describe a session that was still warming up.
function Select-PerfLines([string[]]$Lines) {
    $latest = [ordered]@{}
    foreach ($line in $Lines) {
        if ($line -match 'AXRB\.Perf\s+([a-z0-9-]+):\s*(.*)$') { $latest[$Matches[1]] = $Matches[2].Trim() }
    }
    return $latest
}
function Emit-Perf([string]$Title, $Latest) {
    Emit ''
    if (!$Latest.Count) { Emit ("--- {0} --- (no samples)" -f $Title); return }
    Emit ("--- {0} (most recent 5s window of each counter) ---" -f $Title)
    foreach ($entry in $Latest.GetEnumerator()) { Emit ("  {0,-24} {1}" -f $entry.Key, $entry.Value) }
}
$hostErr = Join-Path $AxrbOut 'logs/game/host.err'
if (Test-Path -LiteralPath $hostErr) {
    $tail = @(Get-Content -LiteralPath $hostErr -Tail 400 -ErrorAction SilentlyContinue)
    Emit-Perf 'host bridge timings' (Select-PerfLines $tail)
    $extent = @($tail | Where-Object { $_ -match 'stereo extent' }) | Select-Object -Last 1
    if ($extent) { Emit ("  {0,-24} {1}" -f 'per-eye extent', ([string]$extent -replace '.*extent\s*', '')) }
    # The bridge overwrites this file per session and stops writing when the
    # game exits, so without the age the numbers above read as current when
    # they can be from last week.
    $age = ([DateTime]::UtcNow - (Get-Item -LiteralPath $hostErr).LastWriteTimeUtc)
    if ($age.TotalSeconds -gt 30) {
        $ago = $(if ($age.TotalHours -ge 1) { '{0:F1} hours' -f $age.TotalHours } else { '{0:F0} minutes' -f $age.TotalMinutes })
        Emit ("  (stale: nothing written for {0}, so this is an earlier session)" -f $ago)
    }
} else { Emit ''; Emit '--- host bridge timings --- (no host.err; has a game been started?)' }

$logcat = Invoke-Adb @('logcat', '-d', '-s', 'AXRB.Perf:I') 20000
Emit-Perf 'guest runtime timings' (Select-PerfLines @($logcat.Text.Split("`n")))
$guestLog = Invoke-Adb @('logcat', '-d', '-s', 'AXRB.GPU:I', 'AXRB.Vulkan:I', 'AXRB.Swapchain:I', 'AXRB.Stereo:I') 20000
if ($guestLog.Code -eq 0 -and $guestLog.Text.Trim()) {
    Emit ''
    Emit '--- guest graphics setup (last lines) ---'
    foreach ($line in (@($guestLog.Text.Split("`n") | Where-Object { $_.Trim() }) | Select-Object -Last 12)) { Emit ("  {0}" -f $line.Trim()) }
}

# ---------------------------------------------------------------- observations
# Only rules that the numbers above can actually settle. Anything softer than
# that belongs in a conversation, not in a report someone will act on alone.
Emit ''
Emit '--- observations ---'
$notes = @()
if (!$gamePid) { $notes += 'No game process was sampled, so the guest and timing sections describe an idle system. Start a game, put the headset on, and run this again.' }
$line = @($report | Where-Object { $_ -match 'supersampleScale=([\d.]+)' }) | Select-Object -First 1
if ($line -match 'supersampleScale=([\d.]+)' -and [double]$Matches[1] -gt 1.05) {
    $scale = [double]$Matches[1]
    $notes += ("SteamVR supersampling is set to {0} (an area multiplier: {1:N2}x more pixels per edge). Every one of those pixels is rendered inside the guest, so this is the first thing to lower." -f $scale, [Math]::Sqrt($scale))
}
if ($hostTotal / [Math]::Max(1, $logical) -gt 85) { $notes += 'Windows is CPU-saturated, so the guest and the compositor are competing for cores. Lower the vCPU count or close background work.' }
if ($gpu) {
    $peak = 0.0
    foreach ($value in $gpu.Engines.Values) { if ($value -gt $peak) { $peak = $value } }
    if ($peak -gt 85) { $notes += ('The busiest GPU engine is at {0:N0}%, so the graphics card is the limit. Lower the render resolution.' -f $peak) }
}
if ($script:topThread -and $script:vcpus) {
    $headroom = $script:vcpus * 100 - $script:guestBusy
    if ($script:topThread.Total -gt 80 -and $headroom -gt 120) {
        # User time is translated game code; system time is syscalls, which for
        # a Vulkan title means the gfxstream pipe. They point at different
        # fixes, so the split decides which sentence is honest here.
        $where = $(if ($script:topThread.User -gt $script:topThread.Total * 0.6) {
            'translated ARM game code, which no amount of extra vCPUs can split up'
        } else {
            'kernel time, which for a Vulkan title is the graphics pipe out of the guest'
        })
        $notes += ("{0} is using {1:N0}% of one core while {2:N1} vCPUs sit idle. One thread is the limit, and it is spending that time in {3}." -f $script:topThread.Label, $script:topThread.Total, ($headroom / 100), $where)
    }
    if ($script:guestBusy -gt $script:vcpus * 85) {
        $notes += ('The guest is using nearly all {0} vCPUs. Raising the vCPU count may help, up to the emulator maximum of 6 and the free cores this PC has.' -f $script:vcpus)
    }
    if ($script:topThread.Involuntary -gt 200) {
        $notes += ('{0} is being preempted {1:N0} times a second, which points at host contention rather than guest work.' -f $script:topThread.Label, $script:topThread.Involuntary)
    }
}
if (!$notes.Count) { $notes += 'Nothing stands out automatically. Send this report along with what the frame rate felt like.' }
foreach ($note in $notes) { Emit ("  * {0}" -f $note) }

$text = (Protect-Text ($report -join "`n")) + "`n"
if ($OutFile) {
    [System.IO.File]::WriteAllText($OutFile, $text, [System.Text.UTF8Encoding]::new($false))
    [Console]::Out.WriteLine("Wrote $OutFile")
} else {
    [Console]::Out.Write($text)
}
