# Builds AXRB Direct (direct/) and stages what the launcher ships in out/direct:
#   axrb-direct.exe, art/ (AOSP bootstrap and host service bridges),
#   guest/ (arm64 launcher and system-library proxies, AXRB's OpenXR runtime).
# The Android system root is not staged: the launcher builds it on the
# player's machine from the SDK system image (direct/tools/make_sysroot.py).
# Needs the NDK, build-tools 35.0.0, platforms;android-36 and a JDK; run after
# runtime/apk/build_apk.ps1 -Abi arm64-v8a, which builds libopenxr_runtime.so.
param(
    [string]$BuildDirectory,
    [string]$RuntimeLibrary,
    [string]$ArtSysroot,
    [string]$Sdk = "$env:LOCALAPPDATA/Android/Sdk"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../paths.ps1"
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
if (!$BuildDirectory) { $BuildDirectory = Join-Path $AxrbRoot 'out/distribution/build-direct' }
if (!$RuntimeLibrary) { $RuntimeLibrary = Join-Path $AxrbRoot 'out/android/runtime-arm64-v8a/runtime/runtime/src/libopenxr_runtime.so' }
if (!(Test-Path -LiteralPath $RuntimeLibrary)) { throw "Build the arm64 OpenXR runtime first (runtime/apk/build_apk.ps1 -Abi arm64-v8a): $RuntimeLibrary is missing." }

& "$PSScriptRoot/direct_deps.ps1"
if (!$?) { throw 'AXRB Direct dependencies failed.' }

if (!$ArtSysroot) {
    $ArtSysroot = Join-Path $AxrbRoot 'out/deps/aosp-36-r02-art'
    if (!(Test-Path -LiteralPath (Join-Path $ArtSysroot '.complete'))) {
        $archive = Join-Path $AxrbRoot 'out/deps/arm64-v8a-36_r02.zip'
        $digest = '62ad6714df790f89c8a8ad32552ffe20bb16fe87'
        if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash -ne $digest) {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -UseBasicParsing -Uri 'https://dl.google.com/android/repository/sys-img/android/arm64-v8a-36_r02.zip' -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash -ne $digest) { throw 'AOSP image checksum mismatch.' }
        $resolved = [IO.Path]::GetFullPath($ArtSysroot)
        $expected = [IO.Path]::GetFullPath((Join-Path $AxrbRoot 'out/deps/aosp-36-r02-art'))
        if ($resolved -ne $expected) { throw 'Unexpected AOSP build directory.' }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
        Run python @((Join-Path $AxrbRoot 'direct/tools/make_sysroot.py'), $archive, $ArtSysroot, '--art')
        Set-Content -LiteralPath (Join-Path $ArtSysroot '.complete') -Value $digest
    }
}
$ndk = if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else {
    (Get-ChildItem -LiteralPath (Join-Path $Sdk 'ndk') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
}
Run python @((Join-Path $AxrbRoot 'direct/tools/build_art_native.py'), '--ndk', $ndk,
    '--sysroot', $ArtSysroot, '--output', (Join-Path $BuildDirectory 'art/lib64'))
Run python @((Join-Path $AxrbRoot 'direct/tools/build_art_bootstrap.py'),
    '--android-jar', (Join-Path $Sdk 'platforms/android-36/android.jar'),
    '--d8', (Join-Path $Sdk 'build-tools/35.0.0/lib/d8.jar'), '--output', (Join-Path $BuildDirectory 'art/bootstrap'))

Run cmake @('-S', (Join-Path $AxrbRoot 'direct'), '-B', $BuildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DAXRB_OPENXR_RUNTIME_SO=$RuntimeLibrary", "-DAXRB_NDK=$ndk", '-DAXRB_GPU_LAYER_DLL=', '-DAXRB_BUILD_LEGACY_BOOT=OFF')
Run cmake @('--build', $BuildDirectory, '--config', 'Release', '--target', 'axrb-direct', '--parallel')

$stage = [IO.Path]::GetFullPath((Join-Path $AxrbRoot 'out/direct.new'))
if (!(($stage + '\').StartsWith([IO.Path]::GetFullPath((Join-Path $AxrbRoot 'out')) + '\', [StringComparison]::OrdinalIgnoreCase))) { throw 'Unexpected Direct staging directory.' }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage/art/lib64", "$stage/guest/lib64" | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release/axrb-direct.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'art/bootstrap/dex/classes.dex') -Destination "$stage/art/axrb-bootstrap.dex"
foreach ($name in 'libandroid.so', 'libaxrbndk.so', 'libmediandk.so', 'libaxrbmndk.so', 'libaxrb_art_bridge.so', 'libOpenSLES.so') {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory "art/lib64/$name") -Destination "$stage/art/lib64"
}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'art/lib64/AOSP-NOTICE.xml.gz') -Destination "$stage/art/AOSP-NOTICE.xml.gz"
Copy-Item -Path (Join-Path $BuildDirectory 'guest/lib64/*.so') -Destination "$stage/guest/lib64"
if (!(Test-Path -LiteralPath "$stage/guest/lib64/libopenxr_runtime.so")) { throw 'libopenxr_runtime.so was not staged.' }
# Older Oculus loaders find the runtime through this package; built beside the runtime it carries.
$systemDriver = Join-Path $AxrbRoot 'out/android/runtime-arm64-v8a/axrb-systemdriver-debug.apk'
if (!(Test-Path -LiteralPath $systemDriver)) { throw "The legacy Oculus driver APK is missing: $systemDriver. Build the runtime with runtime/apk/build_apk.ps1 -Abi arm64-v8a." }
Copy-Item -LiteralPath $systemDriver -Destination "$stage/guest/axrb-systemdriver.apk"
Get-ChildItem -LiteralPath $stage -Recurse -File | Select-Object @{ n = 'File'; e = { $_.FullName.Substring($stage.Length + 1) } }, Length
$destination = [IO.Path]::GetFullPath((Join-Path $AxrbRoot 'out/direct'))
if (!(($destination + '\').StartsWith([IO.Path]::GetFullPath((Join-Path $AxrbRoot 'out')) + '\', [StringComparison]::OrdinalIgnoreCase))) { throw 'Unexpected Direct output directory.' }
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Move-Item -LiteralPath $stage -Destination $destination
exit 0
