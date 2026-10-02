<#
.SYNOPSIS
Restores an AXRB checkout from a backup made by offline_backup.ps1 and
prepares it to build without internet access.

.DESCRIPTION
Clones the backed-up history, restores the runtime signing key, places the
pinned build downloads where the build scripts look for them, restores the
Android development SDK and points git, npm and Electron at the backup's
mirrors and caches. Dot-source it with -EnvOnly to set up the current
PowerShell session for offline builds of an existing checkout.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File offline_restore.ps1 -Checkout C:\AXRB -Build
.EXAMPLE
. D:\AXRB-Backup\offline_restore.ps1 -Checkout C:\AXRB -EnvOnly
#>
param(
    [string]$Backup = $PSScriptRoot,
    [Parameter(Mandatory = $true)][string]$Checkout,
    [string]$Branch = 'windows-android16',
    [ValidateSet('fork', 'upstream')][string]$From = 'fork',
    [switch]$Build,
    [switch]$EnvOnly
)
$ErrorActionPreference = 'Stop'
$Backup = [IO.Path]::GetFullPath($Backup)
$Checkout = [IO.Path]::GetFullPath($Checkout)
if (!(Test-Path -LiteralPath (Join-Path $Backup 'git'))) {
    # Running the copy that lives in a checkout's scripts/backup folder.
    throw "No backup found at $Backup. Pass -Backup <backup folder>."
}
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program $($Arguments -join ' ') failed ($LASTEXITCODE)." }
}
function FileUrl([string]$Path) { 'file:///' + ($Path -replace '\\', '/') }
function Place([string]$Source, [string]$Target) {
    if (!(Test-Path -LiteralPath $Source)) { Write-Warning "Not in backup: $Source"; return }
    if (Test-Path -LiteralPath $Target) { return }
    New-Item -ItemType Directory -Force (Split-Path $Target -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Target
    Write-Host "placed  $Target"
}

$git = Join-Path $Backup 'git'
$mirrors = @{ fork = Join-Path $git 'AXRB-fork.git'; upstream = Join-Path $git 'AXRB-upstream.git' }
if (!(Test-Path -LiteralPath $mirrors[$From])) { $From = 'upstream' }

if (!$EnvOnly) {
    # --- Source -------------------------------------------------------------
    if (!(Test-Path -LiteralPath (Join-Path $Checkout '.git'))) {
        $source = $mirrors[$From]
        if (!(Test-Path -LiteralPath $source)) { $source = Join-Path $Backup "bundles/AXRB-$From.bundle" }
        Run git @('clone', '--branch', $Branch, $source, $Checkout)
        foreach ($name in $mirrors.Keys) {
            if ($name -ne $From -and (Test-Path -LiteralPath $mirrors[$name])) { Run git @('-C', $Checkout, 'remote', 'add', $name, $mirrors[$name]) }
        }
        Run git @('-C', $Checkout, 'fetch', '--all', '--tags')
        Write-Host "Checked out $Branch into $Checkout. Other branches: git -C `"$Checkout`" branch -r"
    } else { Write-Host "Using existing checkout $Checkout" }

    # --- Local state and pinned downloads -----------------------------------
    Place (Join-Path $Backup 'keys/runtime.keystore') (Join-Path $Checkout '.local/keys/runtime.keystore')
    $build = Join-Path $Backup 'downloads/build'
    Place (Join-Path $build 'OpenXR.Loader.1.1.60.nupkg') (Join-Path $Checkout 'out/distribution/OpenXR.Loader.1.1.60.nupkg')
    Place (Join-Path $build 'python-3.14.3-embed-amd64.zip') (Join-Path $Checkout 'out/distribution/python.zip')
    Place (Join-Path $build 'commandlinetools-win-15859902_latest.zip') (Join-Path $Checkout 'out/deps/android-sdk-tools/commandlinetools-win-15859902_latest.zip')
    Place (Join-Path $build 'ispc-v1.31.0-windows.zip') (Join-Path $Checkout 'out/tools/ispc-v1.31.0-windows.zip')
    Place (Join-Path $build 'boost_1_84_0.tar.bz2') (Join-Path $Checkout 'third_party/boost_1_84_0.tar.bz2')

    # --- Android development SDK (the build reads %LOCALAPPDATA%\Android\Sdk) ---
    $sdkBackup = Join-Path $Backup 'android-sdk'
    $sdk = Join-Path $env:LOCALAPPDATA 'Android/Sdk'
    if (Test-Path -LiteralPath $sdkBackup) {
        foreach ($part in @('cmdline-tools/latest', 'platform-tools', 'platforms/android-29', 'build-tools/36.1.0', 'ndk/27.3.13750724', 'cmake/3.22.1', 'licenses')) {
            $partSource = Join-Path $sdkBackup $part; $partTarget = Join-Path $sdk $part
            if ((Test-Path -LiteralPath $partSource) -and !(Test-Path -LiteralPath $partTarget)) {
                Write-Host "restore sdk/$part"
                & robocopy.exe $partSource $partTarget /E /NFL /NDL /NJH /NJS /NP | Out-Null
                if ($LASTEXITCODE -ge 8) { throw "SDK copy failed: $part" }
                $global:LASTEXITCODE = 0
            }
        }
    } else { Write-Warning 'The backup has no Android SDK; install it with scripts/build/android_sdk.ps1 -AcceptLicense.' }
}

# --- Offline environment ----------------------------------------------------
# Redirect every git URL the build clones to the backup's mirrors. These
# GIT_CONFIG_* variables only affect this PowerShell session (git 2.31+).
$redirects = [ordered]@{
    'https://github.com/KhronosGroup/OpenXR-SDK.git'        = Join-Path $git 'deps/OpenXR-SDK.git'
    'https://github.com/TsudaKageyu/minhook.git'            = Join-Path $git 'deps/minhook.git'
    'https://github.com/lioncash/dynarmic.git'              = Join-Path $git 'deps/dynarmic.git'
    'https://github.com/KhronosGroup/OpenXR-SDK-Source.git' = Join-Path $git 'deps/OpenXR-SDK-Source.git'
}
$index = 0
foreach ($url in $redirects.Keys) {
    if (!(Test-Path -LiteralPath $redirects[$url])) { continue }
    Set-Item "env:GIT_CONFIG_KEY_$index" "url.$(FileUrl $redirects[$url]).insteadOf"
    Set-Item "env:GIT_CONFIG_VALUE_$index" $url
    $index++
}
$env:GIT_CONFIG_COUNT = "$index"
$env:npm_config_cache = Join-Path $Backup 'caches/npm'
$env:npm_config_prefer_offline = 'true'
$env:electron_config_cache = Join-Path $Backup 'caches/electron'
$env:ELECTRON_CACHE = Join-Path $Backup 'caches/electron'
$env:ELECTRON_BUILDER_CACHE = Join-Path $Backup 'caches/electron-builder'
Write-Host 'Offline build environment set: git mirrors, npm cache and Electron caches point into the backup.'

if ($Build) {
    Run powershell.exe @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Checkout 'scripts/build/build-launcher.ps1'), '-Portable', '-SkipTests')
    Write-Host "Built: $(Join-Path $Checkout 'out/releases')" -ForegroundColor Green
}
Write-Host @"

First-run Android setup without internet: in the launcher's setup screen, choose
the archives in $(Join-Path $Backup 'downloads\android-components') instead of downloading.
"@
