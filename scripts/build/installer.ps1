param([switch]$SkipBuild, [switch]$Portable, [switch]$Setup, [switch]$PerformanceOverlay)
$ErrorActionPreference = 'Stop'
if ($Portable -and $Setup) { throw 'Choose either -Portable or -Setup, not both.' }
. "$PSScriptRoot/../paths.ps1"
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
Push-Location $AxrbRoot
try {
    & "$PSScriptRoot/branding.ps1"
    if (!$?) { throw 'Branding build failed' }
    if (!$SkipBuild) {
        Run cmake @('-S', '.', '-B', 'out/distribution/build-host', '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DAXRB_BUILD_ANDROID_RUNTIME=OFF', '-DAXRB_BUILD_TESTS=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', "-DAXRB_ENABLE_PERFORMANCE_OVERLAY=$(if ($PerformanceOverlay) { 'ON' } else { 'OFF' })")
        Run cmake @('--build', 'out/distribution/build-host', '--config', 'Release', '--parallel', '2')
        foreach ($component in @('gpu', 'clock')) {
            Run cmake @('-S', "host/$component", '-B', "out/distribution/build-$component", '-G', 'Visual Studio 17 2022', '-A', 'x64')
            Run cmake @('--build', "out/distribution/build-$component", '--config', 'Release', '--parallel', '2')
        }
        & "$AxrbRoot/runtime/apk/build_apk.ps1" -Abi arm64-v8a
        if (!$?) { throw 'Runtime APK build failed' }
        Run python @('scripts/build/runtime_adapters.py')
    }
    Push-Location launcher
    try {
        Run npm.cmd @('ci')
        if (!$env:AXRB_SKIP_LAUNCHER_TESTS) { Run npm.cmd @('test') }
        Run npm.cmd @('run', 'build')
        Run node @('prepare-release.mjs')
        # win-unpacked is shared across modes; the direct-launch EXE keeps its profile outside the app.
        $staleMarker = Join-Path $AxrbRoot 'out/releases/win-unpacked/AXRB.portable'
        if (Test-Path -LiteralPath $staleMarker) { Remove-Item -LiteralPath $staleMarker -Force }
        if ($Portable) {
            # A portable build is the unpacked app plus a marker that keeps its data beside AXRB.exe.
            Run npm.cmd @('exec', '--', 'electron-builder', '--win', 'dir', '--x64', '--publish', 'never')
            $package = Get-Content -LiteralPath 'package.json' -Raw | ConvertFrom-Json
            $unpacked = Join-Path $AxrbRoot 'out/releases/win-unpacked'
            $archive = Join-Path $AxrbRoot "out/releases/AXRB-Portable-$($package.version).zip"
            Set-Content -LiteralPath (Join-Path $unpacked 'AXRB.portable') -Value 'Settings, downloads and the Android runtime are stored in this folder.' -Encoding ascii
            if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [IO.Compression.ZipFile]::CreateFromDirectory($unpacked, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
        }
        # The installed app keeps its profile in %APPDATA%, so the direct-launch
        # marker has to stay out of an installed copy as much as a packed one.
        elseif ($Setup) { Run npm.cmd @('exec', '--', 'electron-builder', '--win', 'nsis', '--x64', '--publish', 'never') }
        else { Run npm.cmd @('exec', '--', 'electron-builder', '--win', 'portable', '--x64', '--publish', 'never') }
    } finally { Pop-Location }
    Run python @('scripts/build/source_archive.py')
} finally { Pop-Location }
