param(
    [string]$AndroidNdk = $env:ANDROID_NDK_HOME,
    [ValidateRange(1, 100000000)][int]$Iterations = 1000000,
    [ValidateRange(1, 8)][int]$BuildJobs = 2,
    [switch]$RequireLse,
    [switch]$Android,
    [string]$Adb = (Join-Path $env:LOCALAPPDATA 'AXRB Runtime/sdk/platform-tools/adb.exe'),
    [ValidateRange(1,65535)][int]$AdbServerPort = 5038,
    [string]$Serial = 'emulator-5584'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deps = Join-Path $repoRoot 'third_party'
$source = Join-Path $deps 'dynarmic'
$boost = Join-Path $deps 'boost_1_84_0'
$build = Join-Path $repoRoot 'out/dynarmic'
if ($Android) { $build = Join-Path $repoRoot 'out/dynarmic-android' }
$revision = 'a41c380246d3d9f9874f0f792d234dc0cc17c180'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if (-not $AndroidNdk) {
    $ndkRoot = Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk'
    $installed = @(Get-ChildItem -LiteralPath $ndkRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe') } |
        Sort-Object { [version]$_.Name } -Descending)
    if ($installed.Count) { $AndroidNdk = $installed[0].FullName }
}
if (-not $AndroidNdk -or -not (Test-Path (Join-Path $AndroidNdk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'))) {
    throw 'Pass -AndroidNdk pointing to an installed Android NDK.'
}
New-Item -ItemType Directory -Force -Path $deps, $build | Out-Null
if (-not (Test-Path $source)) {
    Invoke-Checked git @('clone', 'https://github.com/lioncash/dynarmic.git', $source)
    Invoke-Checked git @('-C', $source, 'checkout', '--detach', $revision)
}
$actual = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -ne $revision) {
    throw "Dynarmic must be at $revision; existing checkout was left untouched."
}
$dirty = & git -C $source status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Dynarmic checkout has changes; refusing an unpinned test.' }
if (-not (Test-Path (Join-Path $boost 'boost/version.hpp'))) {
    $archive = Join-Path $deps 'boost_1_84_0.tar.bz2'
    $hash = 'cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454'
    if (-not (Test-Path $archive)) {
        Invoke-Checked curl.exe @('-L', '--fail', '--output', $archive,
            'https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2')
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
        throw "Boost archive checksum mismatch: $archive"
    }
    Invoke-Checked tar.exe @('-xf', $archive, '-C', $deps, 'boost_1_84_0/boost')
}
if ($Android) {
    $sdkRoot = Split-Path (Split-Path $AndroidNdk)
    $ninja = @(Get-ChildItem -Path (Join-Path $sdkRoot 'cmake/*/bin/ninja.exe') -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1)
    if (-not $ninja.Count) { throw 'Install an Android SDK CMake package (for Ninja).' }
    Invoke-Checked cmake @('-S', (Join-Path $repoRoot 'experimental/dynarmic'), '-B', $build,
        '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$($ninja[0].FullName)",
        "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake",
        '-DANDROID_ABI=x86_64', '-DANDROID_PLATFORM=android-29', '-DANDROID_STL=c++_static',
        '-DCMAKE_BUILD_TYPE=Release', "-DAXRB_NDK=$AndroidNdk", "-DBOOST_ROOT=$boost", "-DBoost_INCLUDE_DIR=$boost", '-DBoost_NO_BOOST_CMAKE=ON')
    Invoke-Checked cmake @('--build', $build, '--target', 'axrb-dynarmic-probe', '--parallel', "$BuildJobs")
    Invoke-Checked $Adb @('-P', "$AdbServerPort", '-s', $Serial, 'shell', 'mkdir', '-p', '/data/local/tmp/axrb-dynarmic-probe')
    foreach ($file in @('axrb-dynarmic-probe', 'guest.bin')) {
        Invoke-Checked $Adb @('-P', "$AdbServerPort", '-s', $Serial, 'push', (Join-Path $build $file), "/data/local/tmp/axrb-dynarmic-probe/$file")
    }
    Invoke-Checked $Adb @('-P', "$AdbServerPort", '-s', $Serial, 'shell', 'chmod', '700', '/data/local/tmp/axrb-dynarmic-probe/axrb-dynarmic-probe')
} else {
    Invoke-Checked cmake @('-S', (Join-Path $repoRoot 'experimental/dynarmic'), '-B', $build,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DAXRB_NDK=$AndroidNdk",
        "-DBOOST_ROOT=$boost", '-DBoost_NO_BOOST_CMAKE=ON')
    $previousCl = $env:_CL_
    try {
        # Bound compiler concurrency even though upstream enables /MP.
        $env:_CL_ = "$previousCl /MP$BuildJobs"
        Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--parallel', "$BuildJobs")
    } finally { $env:_CL_ = $previousCl }
    Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--timeout', '120')
}
$exe = Join-Path $build 'Release/axrb-dynarmic-probe.exe'
$report = Join-Path $build ("probe-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
@("Dynarmic revision: $revision", "Build: $(if ($Android) {'Android x86_64'} else {'Windows x64'}) Release; Boost 1.84.0", "Date: $(Get-Date -Format o)") |
    Set-Content -LiteralPath $report -Encoding UTF8
$lseFailed = $false
foreach ($mode in @('callbacks', 'page-table')) {
    $probeArgs = @('--iterations', "$Iterations")
    if ($mode -eq 'page-table') { $probeArgs += '--page-table' }
    if ($RequireLse) { $probeArgs += '--require-lse' }
    if ($Android) {
        & $Adb -P $AdbServerPort -s $Serial shell /data/local/tmp/axrb-dynarmic-probe/axrb-dynarmic-probe @probeArgs |
            Tee-Object -Variable probeOutput
    } else {
        & $exe @probeArgs | Tee-Object -Variable probeOutput
    }
    $code = $LASTEXITCODE
    $probeOutput | Add-Content -LiteralPath $report -Encoding UTF8
    if ($code -eq 2 -and $RequireLse) { $lseFailed = $true }
    elseif ($code -ne 0) { throw "Dynarmic probe failed ($code); see $report" }
}
Write-Host "Results: $report"
if ($lseFailed) { throw 'Required LSE instructions are unsupported; see the capability report.' }
