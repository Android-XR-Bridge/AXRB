param(
    [switch]$SkipNative,
    [switch]$SkipTests,
    [switch]$KeepUnpacked,
    [switch]$Portable,
    [switch]$Setup,
    [switch]$PerformanceOverlay
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../paths.ps1"
$package = Get-Content -LiteralPath (Join-Path $AxrbRoot 'launcher/package.json') -Raw | ConvertFrom-Json
$version = [string]$package.version
if ([string]::IsNullOrWhiteSpace($version)) { throw 'launcher/package.json has no version.' }

Push-Location $AxrbRoot
try {
    if ($SkipTests) { $env:AXRB_SKIP_LAUNCHER_TESTS = '1' }
    & "$PSScriptRoot/installer.ps1" -SkipBuild:$SkipNative -Portable:$Portable -Setup:$Setup -PerformanceOverlay:$PerformanceOverlay
    if ($LASTEXITCODE -ne 0) { throw "Installer build failed ($LASTEXITCODE)." }

    $release = Join-Path $AxrbRoot 'out/releases'
    $installer = Join-Path $release $(if ($Portable) { "AXRB-Portable-$version.zip" } elseif ($Setup) { "AXRB-Setup-$version.exe" } else { "AXRB-$version.exe" })
    $source = Join-Path $release "AXRB-$version-source.zip"
    foreach ($file in @($installer, $source)) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Expected build artifact is missing: $file" }
    }
    if (!$KeepUnpacked) {
        $unpacked = Join-Path $release 'win-unpacked'
        if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
    }
    foreach ($file in @($installer, $source, (Join-Path $release "SHA256SUMS-$version$(if ($Portable) { '-portable' } elseif ($Setup) { '-setup' }).txt"))) {
        Get-Item -LiteralPath $file | Select-Object Name, Length, LastWriteTime
    }
}
finally {
    Remove-Item Env:AXRB_SKIP_LAUNCHER_TESTS -ErrorAction SilentlyContinue
    Pop-Location
}
