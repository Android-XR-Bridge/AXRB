param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Jdk,
    [switch]$AcceptLicense
)
$ErrorActionPreference = 'Stop'
if (!$AcceptLicense) {
    throw 'Read https://developer.android.com/studio/terms, then rerun with -AcceptLicense to install the Android development tools.'
}
if (!$Jdk) {
    if ($env:JAVA_HOME) { $Jdk = $env:JAVA_HOME }
    else {
        $javac = Get-Command javac.exe -ErrorAction SilentlyContinue
        if ($javac) { $Jdk = Split-Path (Split-Path $javac.Source -Parent) -Parent }
    }
}
if (!$Jdk -or !(Test-Path -LiteralPath "$Jdk\bin\javac.exe")) {
    throw 'Install JDK 17 or newer, then supply -Jdk or set JAVA_HOME.'
}
$Sdk = [IO.Path]::GetFullPath($Sdk)
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tools = Join-Path $Sdk 'cmdline-tools/latest'
$manager = Join-Path $tools 'bin/sdkmanager.bat'
$oldJavaHome = $env:JAVA_HOME
try {
    $env:JAVA_HOME = $Jdk
    if (!(Test-Path -LiteralPath $manager)) {
        if (Test-Path -LiteralPath $tools) { throw "Incomplete command-line tools directory: $tools. Choose another SDK or repair it first." }
        $cache = Join-Path $root 'out/deps/android-sdk-tools'
        New-Item -ItemType Directory -Force $cache | Out-Null
        $archive = Join-Path $cache 'commandlinetools-win-15859902_latest.zip'
        if (!(Test-Path -LiteralPath $archive)) {
            & curl.exe --fail --location --output "$archive.part" 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip'
            if ($LASTEXITCODE -ne 0) { throw 'Android command-line tools download failed.' }
            Move-Item -LiteralPath "$archive.part" -Destination $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a') {
            throw "Android command-line tools checksum mismatch. Remove $archive and retry."
        }
        $staging = Join-Path $cache 'unpacked'
        Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force
        New-Item -ItemType Directory -Force (Split-Path $tools -Parent) | Out-Null
        Move-Item -LiteralPath (Join-Path $staging 'cmdline-tools') -Destination $tools
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    $packages = @('platform-tools', 'platforms;android-29', 'build-tools;36.1.0', 'ndk;27.3.13750724', 'cmake;3.22.1')
    # Answer only the requested packages' license prompts, with explicit consent.
    1..20 | ForEach-Object { 'y' } | & $manager "--sdk_root=$Sdk" @packages
    if ($LASTEXITCODE -ne 0) { throw 'Android development SDK installation failed.' }
    foreach ($relative in @('platform-tools/adb.exe', 'platforms/android-29/android.jar', 'build-tools/36.1.0/apksigner.bat',
            'ndk/27.3.13750724/build/cmake/android.toolchain.cmake', 'cmake/3.22.1/bin/ninja.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $Sdk $relative))) { throw "SDK installation incomplete: $relative" }
    }
    Write-Host "Android development SDK ready: $Sdk"
    Write-Host "JDK: $Jdk"
} finally { $env:JAVA_HOME = $oldJavaHome }
