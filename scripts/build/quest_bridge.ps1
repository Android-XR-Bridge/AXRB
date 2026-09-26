param([string]$Ndk = $env:ANDROID_NDK_HOME, [string]$Generator = 'Visual Studio 17 2022', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath("$PSScriptRoot/../..")
if (!$Ndk) {
    $sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$env:LOCALAPPDATA/Android/Sdk" }
    $Ndk = (Get-ChildItem -LiteralPath "$sdk/ndk" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
}
if (!(Test-Path -LiteralPath "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe")) { throw 'Provide an installed Android NDK with -Ndk.' }
function Run([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE)" }
}
Run cmake @('-S', "$root/experimental/quest-bridge", '-B', "$root/out/quest-bridge", '-G', $Generator, '-A', 'x64', "-DQB_ANDROID_NDK=$Ndk")
Run cmake @('--build', "$root/out/quest-bridge", '--config', 'Release', '--target', 'qb-test', '--parallel')
if (!$SkipTests) { Run ctest @('--test-dir', "$root/out/quest-bridge", '-C', 'Release', '--output-on-failure') }
