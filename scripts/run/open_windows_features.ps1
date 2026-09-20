param(
    [string]$WindowsDir = $env:WINDIR
)
# The launcher reads this script's output as UTF-8 and reports a failure from
# its message alone, so progress records stay out of the stream and a
# terminating error is reduced to its text instead of a full error record.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
trap { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }

if (!$WindowsDir) { $WindowsDir = 'C:\Windows' }
$exe = Join-Path $WindowsDir 'System32\optionalfeatures.exe'
if (![System.IO.Path]::IsPathRooted($exe)) { throw 'Windows Features path is invalid.' }
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Windows Features is unavailable on this Windows installation.' }
# Shell activation, so the dialog owns its window and outlives this script
# rather than being a child the launcher would have to keep alive.
Start-Process -LiteralPath $exe -WindowStyle Normal
