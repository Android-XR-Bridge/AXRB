param(
    [Parameter(Mandatory)][ValidatePattern('^Local\\AXRB\.FpsHud\.[a-f0-9]{32}$')][string]$EventName,
    [Parameter(Mandatory)][ValidateSet(0,1)][int]$Enabled
)
# See run_windows_game.ps1 for why these are set here rather than by the caller.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
$eventHandle = $null
$deadline = [DateTime]::UtcNow.AddSeconds(2)
do {
    try { $eventHandle = [System.Threading.EventWaitHandle]::OpenExisting($EventName) }
    catch [System.Threading.WaitHandleCannotBeOpenedException] {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'The game session is not available for changing its FPS HUD.' }
        Start-Sleep -Milliseconds 50
    }
} while (!$eventHandle)
try { if ($Enabled) { $null = $eventHandle.Set() } else { $null = $eventHandle.Reset() } }
finally { $eventHandle.Dispose() }
