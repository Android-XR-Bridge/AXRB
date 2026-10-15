<#
  Builds the managed boot class path (ADR 0004): the in-tree core library and
  framework, compiled from Java source with javac and converted to DEX with d8.

  JDK and Android build-tools are BUILD-time dependencies only (toolchain.json
  fixture_toolchain). The runner ships the resulting .dex files and never needs
  either tool.

  javac flags:
    --system=none               compile against our own java.lang, not the JDK's
    --patch-module java.base=   ...by compiling the sources as module java.base
                                (javac 9+ has no other way to replace java.lang)
    -source 11 -target 11       d8 in build-tools 35 rejects newer class files
    -XDstringConcat=inline      string '+' as StringBuilder chains, no
                                invokedynamic/StringConcatFactory
  Usage:
    .\tools\build-bootclasspath.ps1 -OutDir build/Debug/bin/boot
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$OutDir,
  [string]$BuildTools = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$cfg  = Get-Content (Join-Path $repo 'tools/toolchain.json') -Raw | ConvertFrom-Json
if (-not $BuildTools) { $BuildTools = $cfg.fixture_toolchain.build_tools }

function Invoke-Native {
  param([Parameter(Mandatory)][string]$Exe, [object[]]$Arguments, [string]$What)
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $prev }
  if ($code -ne 0) { throw ("{0} failed (exit {1})" -f $What, $code) }
}

function Resolve-Javac {
  if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME 'bin/javac.exe'))) {
    return (Join-Path $env:JAVA_HOME 'bin/javac.exe')
  }
  $pinned = "C:/Program Files/Java/jdk-$($cfg.fixture_toolchain.jdk)/bin/javac.exe"
  if (Test-Path $pinned) { return $pinned }
  $found = @(Get-ChildItem 'C:/Program Files/Java' -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin/javac.exe' } |
    Where-Object { Test-Path $_ })
  if ($found.Count -eq 0) { throw 'no JDK with javac found; set JAVA_HOME' }
  return $found[0]
}

$javac = Resolve-Javac
$d8 = Join-Path $env:LOCALAPPDATA "Android/Sdk/build-tools/$BuildTools/d8.bat"
if (-not (Test-Path $d8)) { throw "missing d8: $d8" }

$javaRoot = Join-Path $repo 'managed/java'
# core and framework are one compilation unit: the framework may use any core
# class and the core library references nothing in android.*.
$sources = @(Get-ChildItem -Recurse -Filter '*.java' (Join-Path $javaRoot 'core'), (Join-Path $javaRoot 'framework') -ErrorAction SilentlyContinue |
  ForEach-Object { $_.FullName })
if ($sources.Count -eq 0) { throw "no Java sources under $javaRoot" }

# .NET file APIs resolve relative paths against the process directory, not the
# PowerShell location.
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
New-Item -ItemType Directory -Force $OutDir | Out-Null
$work = Join-Path $OutDir 'obj'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
$classes = Join-Path $work 'classes'
New-Item -ItemType Directory -Force $classes | Out-Null

# The framework resource package first: its generated android.R is part of
# the framework compilation unit.
$gen = Join-Path $work 'gen'
& (Join-Path $PSScriptRoot 'build-framework-res.ps1') -OutDir $OutDir -GenDir $gen -BuildTools $BuildTools
if (-not $?) { throw 'framework resources failed' }
$sources += @(Get-ChildItem -Recurse -Filter '*.java' $gen | ForEach-Object { $_.FullName })

$argFile = Join-Path $work 'sources.txt'
[System.IO.File]::WriteAllLines($argFile, ($sources | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' }))

Invoke-Native $javac -What 'javac' -Arguments @(
  '--system=none', '--patch-module', "java.base=$(Join-Path $javaRoot 'core');$(Join-Path $javaRoot 'framework');$gen",
  '-source', '11', '-target', '11', '-XDstringConcat=inline',
  '-J-Duser.language=en', '-encoding', 'UTF-8', '-nowarn', '-Xlint:-options', '-Xmaxerrs', '500',
  '-d', $classes, "@$argFile")

$classFiles = @(Get-ChildItem -Recurse -Filter '*.class' $classes | ForEach-Object { $_.FullName })
$classList = Join-Path $work 'classes.txt'
[System.IO.File]::WriteAllLines($classList, $classFiles)

$dexOut = Join-Path $work 'dex'
New-Item -ItemType Directory -Force $dexOut | Out-Null
Invoke-Native $d8 -What 'd8' -Arguments @('--min-api', '29', '--release', '--output', $dexOut, "@$classList")

$dexes = @(Get-ChildItem -Filter 'classes*.dex' $dexOut)
if ($dexes.Count -ne 1) { throw "expected one boot dex, got $($dexes.Count)" }
Copy-Item $dexes[0].FullName (Join-Path $OutDir 'arun-boot.dex') -Force
Write-Output ("boot class path: {0} ({1} classes, {2} bytes)" -f (Join-Path $OutDir 'arun-boot.dex'), $classFiles.Count, $dexes[0].Length)
