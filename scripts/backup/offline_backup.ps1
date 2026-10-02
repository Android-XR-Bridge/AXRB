<#
.SYNOPSIS
Creates a self-contained offline backup of AXRB: source history, every pinned
download the build and first-run setup need, the Android development SDK,
npm/Electron caches, release builds and (optionally) toolchain installers and
your downloaded games.

.DESCRIPTION
Run it again at any time: git mirrors are updated in place and downloads whose
checksum already matches are skipped. See docs/offline_backup.md for the
layout and for restoring with scripts/backup/offline_restore.ps1.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/backup/offline_backup.ps1 -Destination D:\AXRB-Backup -AcceptAndroidLicense
#>
param(
    [string]$Destination = (Join-Path $env:USERPROFILE 'AXRB-Offline-Backup'),
    [string]$UpstreamUrl = 'https://github.com/Android-XR-Bridge/AXRB.git',
    [string]$ForkUrl = '',
    # Installs/updates the Android development SDK with sdkmanager. Read
    # https://developer.android.com/studio/terms before passing it.
    [switch]$AcceptAndroidLicense,
    [switch]$SkipAndroidSdk,
    [switch]$SkipBuild,
    [switch]$SkipReleases,
    # Downloads Node, Python, JDK, Git, CMake and a Visual Studio Build Tools
    # offline layout (several GB) so a new PC can be set up without internet.
    [switch]$IncludeToolchains,
    # Folder holding downloaded games (APK/OBB) to copy into the backup.
    [string]$GamesDir = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Destination = [IO.Path]::GetFullPath($Destination)
$warnings = New-Object System.Collections.Generic.List[string]

function Step([string]$Text) { Write-Host "`n=== $Text ===" -ForegroundColor Cyan }
function Warn([string]$Text) { Write-Warning $Text; $warnings.Add($Text) }
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program $($Arguments -join ' ') failed ($LASTEXITCODE)." }
}
function Ensure([string]$Path) { New-Item -ItemType Directory -Force -Path $Path | Out-Null; $Path }
function Test-Hash([string]$File, [string]$Algorithm, [string]$Expected) {
    if (!$Expected) { return (Test-Path -LiteralPath $File -PathType Leaf) }
    if (!(Test-Path -LiteralPath $File -PathType Leaf)) { return $false }
    return (Get-FileHash -LiteralPath $File -Algorithm $Algorithm).Hash -ieq $Expected
}
# curl.exe ships with Windows 10+ and is far faster than Invoke-WebRequest in PowerShell 5.1.
function Get-Pinned([string]$Url, [string]$File, [string]$Algorithm = 'SHA256', [string]$Expected = '') {
    if (Test-Hash $File $Algorithm $Expected) { Write-Host "ok      $(Split-Path $File -Leaf)"; return }
    Ensure (Split-Path $File -Parent) | Out-Null
    Write-Host "fetch   $Url"
    & curl.exe --fail --location --retry 3 --continue-at - --output "$File.part" $Url
    if ($LASTEXITCODE -ne 0) { Remove-Item -LiteralPath "$File.part" -Force -ErrorAction SilentlyContinue; throw "Download failed: $Url" }
    Move-Item -LiteralPath "$File.part" -Destination $File -Force
    if (!(Test-Hash $File $Algorithm $Expected)) { Remove-Item -LiteralPath $File -Force; throw "Checksum mismatch: $Url" }
}
function Update-Mirror([string]$Url, [string]$Directory) {
    if (Test-Path -LiteralPath (Join-Path $Directory 'HEAD')) {
        Write-Host "update  $Url"
        Run git @('-C', $Directory, 'remote', 'set-url', 'origin', $Url)
        Run git @('-C', $Directory, 'remote', 'update', '--prune')
    } else {
        Write-Host "mirror  $Url"
        Run git @('clone', '--mirror', $Url, $Directory)
    }
}
function Copy-Tree([string]$From, [string]$To) {
    # robocopy exit codes below 8 mean success.
    & robocopy.exe $From $To /E /NFL /NDL /NJH /NJS /NP /R:2 /W:2 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copy failed: $From -> $To ($LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
}

foreach ($tool in @('git', 'curl.exe', 'robocopy.exe')) {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool is required." }
}
if (!$ForkUrl) { $ForkUrl = (& git -C $repo remote get-url origin 2>$null) }
Ensure $Destination | Out-Null
Write-Host "Backing up AXRB into $Destination"

# --- 1. Source history ------------------------------------------------------
Step 'Source history (all branches and tags)'
$git = Ensure (Join-Path $Destination 'git')
Update-Mirror $UpstreamUrl (Join-Path $git 'AXRB-upstream.git')
if ($ForkUrl -and $ForkUrl -ne $UpstreamUrl) { Update-Mirror $ForkUrl (Join-Path $git 'AXRB-fork.git') }
# Third-party sources the build clones: CMake FetchContent (OpenXR headers,
# MinHook), the Dynarmic experiment and the hello_xr test.
$deps = @{
    'OpenXR-SDK.git'        = 'https://github.com/KhronosGroup/OpenXR-SDK.git'
    'minhook.git'           = 'https://github.com/TsudaKageyu/minhook.git'
    'dynarmic.git'          = 'https://github.com/lioncash/dynarmic.git'
    'OpenXR-SDK-Source.git' = 'https://github.com/KhronosGroup/OpenXR-SDK-Source.git'
}
foreach ($name in $deps.Keys) {
    try { Update-Mirror $deps[$name] (Join-Path $git "deps/$name") } catch { Warn "Could not mirror $($deps[$name]): $($_.Exception.Message)" }
}
# Single-file bundles survive copying to any drive or cloud folder.
$bundles = Ensure (Join-Path $Destination 'bundles')
foreach ($mirror in Get-ChildItem -LiteralPath $git -Directory -Filter 'AXRB-*.git') {
    Run git @('-C', $mirror.FullName, 'bundle', 'create', (Join-Path $bundles "$($mirror.BaseName).bundle"), '--all')
}
# Your checkout's own branches and commits, including ones never pushed.
Run git @('-C', $repo, 'bundle', 'create', (Join-Path $bundles 'local-checkout.bundle'), '--branches', '--tags')
# Local, gitignored state: the signing key keeps rebuilt runtime APKs
# installable over existing ones.
$keys = Ensure (Join-Path $Destination 'keys')
foreach ($key in @('runtime.keystore', '.local/keys/runtime.keystore')) {
    $file = Join-Path $repo $key
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $keys 'runtime.keystore') -Force; Write-Host "saved   $key" }
}
if (& git -C $repo status --porcelain) {
    Warn 'Your checkout has uncommitted changes; they are saved in source\working-copy.zip but not in git history.'
}
# Tracked plus untracked (not ignored) files, so uncommitted work is kept too.
$snapshot = Join-Path (Ensure (Join-Path $Destination 'source')) 'working-copy.zip'
Remove-Item -LiteralPath $snapshot -Force -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($snapshot, 'Create')
try {
    foreach ($relative in & git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard) {
        $full = Join-Path $repo $relative
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $full, $relative, 'Optimal')
        }
    }
} catch { Warn "Working-copy snapshot failed: $($_.Exception.Message)" }
finally { $zip.Dispose() }

# --- 2. Pinned downloads ----------------------------------------------------
Step 'Pinned downloads (checksum verified)'
$downloads = Ensure (Join-Path $Destination 'downloads')
# First-run Android setup: the launcher can install these offline when you
# choose them in its setup screen.
$components = Get-Content -LiteralPath (Join-Path $repo 'launcher/core/components.json') -Raw | ConvertFrom-Json
foreach ($c in $components) {
    $algorithm = 'SHA256'; $expected = $c.sha256
    if (!$expected) { $algorithm = 'SHA1'; $expected = $c.sha1 }
    Get-Pinned $c.url (Join-Path $downloads "android-components/$(Split-Path $c.url -Leaf)") $algorithm $expected
}
# Build-time downloads, pinned exactly as the build scripts pin them.
$pinned = @(
    @{ Url = 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip'; Sha = '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a' },
    @{ Url = 'https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.60/OpenXR.Loader.1.1.60.nupkg'; Sha = '93c800cfe3269a19683fe6fef51236c16a164c641a6903eff84c6b2816afe556' },
    @{ Url = 'https://www.python.org/ftp/python/3.14.3/python-3.14.3-embed-amd64.zip'; Sha = 'ad4961a479dedbeb7c7d113253f8db1b1935586b73c27488712beec4f2c894e6' },
    @{ Url = 'https://github.com/ispc/ispc/releases/download/v1.31.0/ispc-v1.31.0-windows.zip'; Sha = '9a18793800b91d5be7b851513672cd9a81a985a5a5dfec5611c2318e8ad4140a' },
    @{ Url = 'https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2'; Sha = 'cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454' }
)
foreach ($p in $pinned) {
    try { Get-Pinned $p.Url (Join-Path $downloads "build/$(Split-Path $p.Url -Leaf)") 'SHA256' $p.Sha }
    catch { Warn $_.Exception.Message }
}

# --- 3. Published releases --------------------------------------------------
if (!$SkipReleases) {
    Step 'Published GitHub releases'
    foreach ($url in @($UpstreamUrl, $ForkUrl) | Where-Object { $_ } | Select-Object -Unique) {
        if ($url -notmatch 'github\.com[/:]([^/]+)/([^/]+?)(\.git)?$') { continue }
        $slug = "$($Matches[1])/$($Matches[2])"
        try { $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$slug/releases?per_page=100" -Headers @{ 'User-Agent' = 'AXRB-backup' } }
        catch { Warn "Could not list releases for ${slug}: $($_.Exception.Message)"; continue }
        foreach ($release in $releases) {
            $folder = Ensure (Join-Path $Destination "releases/$($slug -replace '/', '_')/$($release.tag_name)")
            Set-Content -LiteralPath (Join-Path $folder 'RELEASE_NOTES.md') -Value $release.body -Encoding UTF8
            foreach ($asset in $release.assets) {
                $file = Join-Path $folder $asset.name
                if ((Test-Path -LiteralPath $file) -and (Get-Item -LiteralPath $file).Length -eq $asset.size) { Write-Host "ok      $($asset.name)"; continue }
                try { Get-Pinned $asset.browser_download_url $file } catch { Warn $_.Exception.Message }
            }
        }
    }
}

# --- 4. Android development SDK --------------------------------------------
# The build reads the SDK from %LOCALAPPDATA%\Android\Sdk (scripts/build/runtime_adapters.py).
$sdk = Join-Path $env:LOCALAPPDATA 'Android/Sdk'
$sdkParts = @('cmdline-tools/latest', 'platform-tools', 'platforms/android-29', 'build-tools/36.1.0', 'ndk/27.3.13750724', 'cmake/3.22.1', 'licenses')
if (!$SkipAndroidSdk) {
    Step 'Android development SDK'
    if ($AcceptAndroidLicense) {
        Run powershell.exe @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repo 'scripts/build/android_sdk.ps1'), '-AcceptLicense')
    }
    $missing = $sdkParts | Where-Object { !(Test-Path -LiteralPath (Join-Path $sdk $_)) }
    if ($missing) { Warn "Android SDK parts are missing ($($missing -join ', ')). Rerun with -AcceptAndroidLicense to install them." }
    foreach ($part in $sdkParts | Where-Object { $missing -notcontains $_ }) {
        Write-Host "copy    sdk/$part"
        Copy-Tree (Join-Path $sdk $part) (Join-Path $Destination "android-sdk/$part")
    }
}

# --- 5. Build, capturing npm and Electron caches ---------------------------
$caches = @{
    npm_config_cache       = Join-Path $Destination 'caches/npm'
    electron_config_cache  = Join-Path $Destination 'caches/electron'
    ELECTRON_CACHE         = Join-Path $Destination 'caches/electron'
    ELECTRON_BUILDER_CACHE = Join-Path $Destination 'caches/electron-builder'
}
$saved = @{}
foreach ($name in $caches.Keys) { Ensure $caches[$name] | Out-Null; $saved[$name] = [Environment]::GetEnvironmentVariable($name); [Environment]::SetEnvironmentVariable($name, $caches[$name]) }
try {
    Step 'npm dependencies'
    Push-Location (Join-Path $repo 'launcher')
    try { Run npm.cmd @('ci') } catch { Warn "npm ci failed: $($_.Exception.Message)" } finally { Pop-Location }
    if (!$SkipBuild) {
        Step 'Portable release build'
        $version = (Get-Content -LiteralPath (Join-Path $repo 'launcher/package.json') -Raw | ConvertFrom-Json).version
        try {
            Run powershell.exe @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repo 'scripts/build/build-launcher.ps1'), '-Portable', '-SkipTests')
            $builds = Ensure (Join-Path $Destination "builds/$version")
            Get-ChildItem -LiteralPath (Join-Path $repo 'out/releases') -File | Where-Object { $_.Name -match [regex]::Escape($version) } |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $builds -Force; Write-Host "saved   $($_.Name)" }
        } catch { Warn "Build failed; the backup still has everything needed to retry offline. $($_.Exception.Message)" }
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}

# --- 6. Toolchain installers -----------------------------------------------
if ($IncludeToolchains) {
    Step 'Toolchain installers'
    $toolchains = Ensure (Join-Path $Destination 'toolchains')
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        foreach ($id in @('OpenJS.NodeJS.LTS', 'Python.Python.3.12', 'EclipseAdoptium.Temurin.17.JDK', 'Git.Git', 'Kitware.CMake')) {
            & winget download --id $id --exact --accept-source-agreements --accept-package-agreements --download-directory (Join-Path $toolchains $id)
            if ($LASTEXITCODE -ne 0) { Warn "winget could not download $id." }
        }
    } else { Warn 'winget is unavailable; download Node.js 24, Python 3.12, JDK 17, Git and CMake installers manually into toolchains\.' }
    try {
        $vs = Join-Path $toolchains 'vs_buildtools.exe'
        Get-Pinned 'https://aka.ms/vs/17/release/vs_buildtools.exe' $vs
        Write-Host 'Creating the Visual Studio 2022 Build Tools offline layout (large, slow)...'
        $p = Start-Process -FilePath $vs -Wait -PassThru -ArgumentList @('--layout', "`"$(Join-Path $toolchains 'vs2022-buildtools')`"",
            '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended', '--lang', 'en-US', '--passive', '--wait')
        if ($p.ExitCode -ne 0) { Warn "Visual Studio layout exited with $($p.ExitCode)." }
    } catch { Warn "Visual Studio layout failed: $($_.Exception.Message)" }
}

# --- 7. Games ---------------------------------------------------------------
if ($GamesDir) {
    Step 'Downloaded games'
    if (Test-Path -LiteralPath $GamesDir) { Copy-Tree $GamesDir (Join-Path $Destination 'games') }
    else { Warn "Games folder not found: $GamesDir" }
}

# --- 8. Restore kit and manifest -------------------------------------------
Step 'Restore kit and manifest'
foreach ($file in @('scripts/backup/offline_restore.ps1', 'scripts/backup/offline_backup.ps1', 'docs/offline_backup.md')) {
    Copy-Item -LiteralPath (Join-Path $repo $file) -Destination $Destination -Force
}
$manifest = Join-Path $Destination 'SHA256SUMS.txt'
Get-ChildItem -LiteralPath $Destination -File -Recurse |
    Where-Object { $_.FullName -notmatch '\\(git|caches|android-sdk|toolchains\\vs2022-buildtools)\\' -and $_.Name -ne 'SHA256SUMS.txt' } |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($Destination.Length + 1) } |
    Set-Content -LiteralPath $manifest -Encoding ASCII
@"
AXRB offline backup
Created: $(Get-Date -Format s)
Source:  $(& git -C $repo rev-parse HEAD) ($(& git -C $repo rev-parse --abbrev-ref HEAD))
Upstream: $UpstreamUrl
Fork:     $ForkUrl
Restore:  powershell -ExecutionPolicy Bypass -File offline_restore.ps1 -Checkout C:\AXRB -Build
Details:  offline_backup.md
"@ | Set-Content -LiteralPath (Join-Path $Destination 'README.txt') -Encoding UTF8

$size = (Get-ChildItem -LiteralPath $Destination -Recurse -File | Measure-Object Length -Sum).Sum / 1GB
Write-Host ("`nBackup complete: {0} ({1:N1} GB)" -f $Destination, $size) -ForegroundColor Green
if ($warnings.Count) {
    Write-Host "`nFinished with $($warnings.Count) warning(s):" -ForegroundColor Yellow
    $warnings | ForEach-Object { Write-Host " - $_" -ForegroundColor Yellow }
    exit 1
}
