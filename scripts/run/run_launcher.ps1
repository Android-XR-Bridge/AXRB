param([switch]$InstallDependencies, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$launcher = (Resolve-Path "$PSScriptRoot/../../launcher").Path
if (!(Get-Command node -ErrorAction SilentlyContinue)) { throw 'Install Node.js 24 or later, then run this launcher again.' }
if ($InstallDependencies -or !(Test-Path "$launcher/node_modules/electron/package.json") -or !(Test-Path "$launcher/node_modules/vite/package.json") -or !(Test-Path "$launcher/node_modules/react/package.json")) {
    & npm.cmd ci --prefix $launcher --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Launcher dependencies could not be installed.' }
}
$logDirectory = Join-Path (Split-Path $launcher -Parent) 'out/logs/launcher'
New-Item -ItemType Directory -Force $logDirectory | Out-Null
$arguments = @('"' + "$launcher/start.cjs" + '"')
if ($Debug) { $arguments += '--debug' }
Start-Process -FilePath (Get-Command node).Source -WindowStyle Hidden -ArgumentList $arguments -WorkingDirectory $launcher -RedirectStandardOutput "$logDirectory/launcher.log" -RedirectStandardError "$logDirectory/launcher.err" | Out-Null
