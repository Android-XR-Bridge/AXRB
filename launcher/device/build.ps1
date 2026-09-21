param(
    [string]$Sdk = "$env:LOCALAPPDATA/Android/Sdk",
    [string]$Java
)
$ErrorActionPreference = 'Stop'
if (!$Java) {
    # Windows may expose javac through an Oracle javapath shim. Its parent is
    # not a JDK, so validate candidate roots before selecting one.
    $candidates = @()
    if ($env:JAVA_HOME) { $candidates += $env:JAVA_HOME }
    $candidates += "$env:ProgramFiles\Android\Android Studio\jbr"
    $candidates += "$env:ProgramFiles\Java\latest"
    $javac = Get-Command javac.exe -ErrorAction SilentlyContinue
    if ($javac) { $candidates += (Split-Path (Split-Path $javac.Source -Parent) -Parent) }
    $Java = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'bin\javac.exe')) } | Select-Object -First 1
}
if (!$Java -or !(Test-Path -LiteralPath (Join-Path $Java 'bin\javac.exe') -PathType Leaf)) { throw 'A JDK with bin\javac.exe is required. Set JAVA_HOME or pass -Java.' }
. "$PSScriptRoot/../../scripts/paths.ps1"
$build = "$AxrbRoot/out/android/quest-catalog"
$output = "$AxrbRoot/launcher/assets/quest-catalog.jar"
$source = "$AxrbRoot/launcher/device/QuestCatalog.java"
if ((Test-Path $output) -and (Get-Item $output).LastWriteTimeUtc -gt (Get-Item $source).LastWriteTimeUtc) { return }
$platform = Get-ChildItem "$Sdk/platforms" -Directory | Where-Object Name -Match '^android-\d+$' | Sort-Object { [int]($_.Name -replace 'android-', '') } -Descending | Select-Object -First 1
$tools = Get-ChildItem "$Sdk/build-tools" -Directory | Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$platform -or !$tools) { throw 'Android SDK platform and build tools are required for the Quest catalog.' }
New-Item -ItemType Directory -Force "$build/classes", "$AxrbRoot/launcher/assets" | Out-Null
& "$Java/bin/javac.exe" -source 8 -target 8 -classpath "$($platform.FullName)/android.jar" -d "$build/classes" $source
if ($LASTEXITCODE -ne 0) { throw 'Quest catalog Java compilation failed' }
& "$Java/bin/java.exe" -cp "$($tools.FullName)/lib/d8.jar" com.android.tools.r8.D8 --min-api 29 --lib "$($platform.FullName)/android.jar" --output $output "$build/classes/QuestCatalog.class"
if ($LASTEXITCODE -ne 0) { throw 'Quest catalog DEX compilation failed' }
