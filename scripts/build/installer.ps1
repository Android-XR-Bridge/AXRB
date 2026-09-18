param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
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
        Run cmake @('-S', '.', '-B', 'out/distribution/build-host', '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DAXRB_BUILD_ANDROID_RUNTIME=OFF', '-DAXRB_BUILD_TESTS=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
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
        Run npm.cmd @('exec', '--', 'electron-builder', '--win', 'nsis', '--x64')
    } finally { Pop-Location }
    Run python @('scripts/build/source_archive.py')
} finally { Pop-Location }
