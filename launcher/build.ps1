param(
    [switch]$SkipNative,
    [switch]$SkipTests,
    [switch]$KeepUnpacked,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package.json') -Raw | ConvertFrom-Json
$version = [string]$package.version
if ([string]::IsNullOrWhiteSpace($version)) { throw 'launcher/package.json has no version.' }

Push-Location $root
try {
    $script = Join-Path $root 'scripts/build/installer.ps1'
    if (!(Test-Path -LiteralPath $script -PathType Leaf)) { throw "Build implementation is missing: $script" }
    if ($SkipTests) { $env:AXRB_SKIP_LAUNCHER_TESTS = '1' }
    & $script -SkipBuild:$SkipNative -Portable:$Portable
    if ($LASTEXITCODE -ne 0) { throw "Installer build failed ($LASTEXITCODE)." }
    $release = Join-Path $root 'out/releases'
    $artifacts = @(
        (Join-Path $release $(if ($Portable) { "AXRB-Portable-$version.zip" } else { "AXRB-$version.exe" })),
        (Join-Path $release "AXRB-$version-source.zip")
    )
    foreach ($file in $artifacts) { if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing build artifact: $file" } }
    if (!$KeepUnpacked) {
        $unpacked = Join-Path $release 'win-unpacked'
        if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
    }
    foreach ($file in @($artifacts + (Join-Path $release "SHA256SUMS-$version$(if ($Portable) { '-portable' }).txt"))) {
        Get-Item -LiteralPath $file | Select-Object Name, Length, LastWriteTime
    }
}
finally {
    Remove-Item Env:AXRB_SKIP_LAUNCHER_TESTS -ErrorAction SilentlyContinue
    Pop-Location
}
