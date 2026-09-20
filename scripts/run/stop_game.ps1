param(
    # Not named Pid: $PID is an automatic variable for the current process.
    [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$ParentPid
)
# See open_windows_features.ps1 for why these three are set together.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }

# Closing the bridge window is what lets a game save and shut down cleanly, so
# this asks rather than kills. The bridge can also exit on its own between the
# query and the request, which is a normal stop and not a failure.
$bridges = Get-CimInstance Win32_Process -Filter "Name='axrb-host-bridge.exe'" | Where-Object ParentProcessId -eq $ParentPid
foreach ($bridge in $bridges) {
    $process = Get-Process -Id $bridge.ProcessId -ErrorAction SilentlyContinue
    if ($process) { $null = $process.CloseMainWindow() }
}
