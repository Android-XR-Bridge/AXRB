# Fetches the pinned third-party sources AXRB Direct builds against into
# third_party/ (gitignored): Digitalis and Dynarmic with AXRB's patches
# (direct/patches) and the Boost headers Dynarmic needs. Safe to rerun; existing
# checkouts must be at the pinned commits, and patches already applied are kept.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../paths.ps1"

$dynarmicUrl = 'https://github.com/lioncash/dynarmic.git'
$dynarmicCommit = 'a41c380246d3d9f9874f0f792d234dc0cc17c180'
$boost = @{
    Url = 'https://archives.boost.io/release/1.84.0/source/boost_1_84_0.zip'
    Sha256 = 'cc77eb8ed25da4d596b25e77e4dbb6c5afaac9cddd00dc9ca947b6b268cc76a4'
}

function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed ($LASTEXITCODE)" }
}

# Reverse-check failure means the patch has not been applied yet. Windows
# PowerShell 5 turns native stderr into a terminating error under Stop, so
# handle that expected nonzero probe separately from the real apply operation.
function Test-AxrbPatchApplied([string]$Repository, [string]$Patch) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & git -C $Repository apply --reverse --check --ignore-whitespace $Patch 2>$null
        return $LASTEXITCODE -eq 0
    } finally { $ErrorActionPreference = $savedPreference }
}

$thirdParty = Join-Path $AxrbRoot 'third_party'
New-Item -ItemType Directory -Force $thirdParty | Out-Null

# Digitalis's public ARM64 interpreter and lightweight JIT. The Windows adapter
# is built in direct/digitalis; the Android NativeBridge deployment is not used.
$digitalis = Join-Path $thirdParty 'digitalis'
$digitalisCommit = 'f5f1c90b17d0c1df9e9be41534c653a3e74bfc64'
if (!(Test-Path -LiteralPath (Join-Path $digitalis '.git'))) {
    Run git @('clone', '--filter=blob:none', '--no-checkout', 'https://github.com/DigitalisX64/platform_frameworks_libs_binary_translation.git', $digitalis)
    Run git @('-C', $digitalis, 'checkout', '--detach', $digitalisCommit)
}
if ((& git -C $digitalis rev-parse HEAD).Trim() -ne $digitalisCommit) {
    throw "third_party/digitalis must be at $digitalisCommit."
}
foreach ($patch in Get-ChildItem -LiteralPath (Join-Path $AxrbRoot 'direct/patches') -Filter 'digitalis-*.patch' | Sort-Object Name) {
    if (Test-AxrbPatchApplied $digitalis $patch.FullName) { continue }  # already applied
    Run git @('-C', $digitalis, 'apply', '--ignore-whitespace', $patch.FullName)
    Write-Output "Applied $($patch.Name)"
}

# Dynarmic. Its externals are vendored in its own tree, so no submodules.
$dynarmic = Join-Path $thirdParty 'dynarmic'
if (!(Test-Path -LiteralPath (Join-Path $dynarmic '.git'))) {
    Run git @('-c', 'core.autocrlf=false', 'clone', '--filter=blob:none', '--no-checkout', $dynarmicUrl, $dynarmic)
    Run git @('-C', $dynarmic, '-c', 'core.autocrlf=false', 'checkout', '--detach', $dynarmicCommit)
}
$head = (& git -C $dynarmic rev-parse HEAD).Trim()
if ($head -ne $dynarmicCommit) { throw "third_party/dynarmic is at $head; AXRB Direct needs $dynarmicCommit." }
foreach ($patch in Get-ChildItem -LiteralPath (Join-Path $AxrbRoot 'direct/patches') -Filter 'dynarmic-*.patch' | Sort-Object Name) {
    if (Test-AxrbPatchApplied $dynarmic $patch.FullName) { continue }  # already applied
    Run git @('-C', $dynarmic, 'apply', '--ignore-whitespace', $patch.FullName)
    Write-Output "Applied $($patch.Name)"
}

# Boost headers (header-only use).
$boostRoot = Join-Path $thirdParty 'boost_1_84_0'
if (!(Test-Path -LiteralPath (Join-Path $boostRoot 'boost/version.hpp')) -or !(Test-Path -LiteralPath (Join-Path $boostRoot 'LICENSE_1_0.txt'))) {
    $downloads = Join-Path $AxrbRoot 'out/deps'
    New-Item -ItemType Directory -Force $downloads | Out-Null
    $archive = Join-Path $downloads 'boost_1_84_0.zip'
    if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $boost.Sha256) {
        Write-Output "Downloading $($boost.Url)"
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $boost.Url -OutFile $archive -UseBasicParsing
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($hash -ne $boost.Sha256) { Remove-Item -LiteralPath $archive -Force; throw "Boost archive SHA-256 $hash does not match the pinned $($boost.Sha256)." }
    # Just the header tree and the license. (Windows tar rejects a few of the
    # archive's directory entries, so read it with .NET instead.)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (!$entry.Name -or !($entry.FullName.StartsWith('boost_1_84_0/boost/') -or $entry.FullName -eq 'boost_1_84_0/LICENSE_1_0.txt')) { continue }
            $target = Join-Path $thirdParty $entry.FullName
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally { $zip.Dispose() }
}
Write-Output 'AXRB Direct dependencies are ready.'
