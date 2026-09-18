param(
    [string]$Sdk = "$env:LOCALAPPDATA/Android/Sdk",
    [string]$Java = "$env:ProgramFiles/Android/Android Studio/jbr"
)
$ErrorActionPreference = 'Stop'
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
