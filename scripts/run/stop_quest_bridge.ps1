param([Parameter(Mandatory)][string]$EventName, [Parameter(Mandatory)][int]$ProcessId)
$ErrorActionPreference = 'Stop'
if ($EventName -notmatch '^Local\\AXRB\.QuestBridge\.[a-f0-9]{32}$') { throw 'Invalid Quest Bridge stop event.' }
$process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
if (!$process) { exit 0 }
try {
    $event = [Threading.EventWaitHandle]::OpenExisting($EventName)
    try { [void]$event.Set() } finally { $event.Dispose() }
} catch [Threading.WaitHandleCannotBeOpenedException] {
    # A runner still starting may not have created its event yet.
    throw 'Quest Bridge has not created its stop event yet. Try Stop again.'
}
if (!$process.WaitForExit(30000)) {
    Write-Output 'Quest Bridge did not finish its lifecycle shutdown in 30 seconds; forcing exit. Saves are not confirmed.'
    $process.Kill()
}
