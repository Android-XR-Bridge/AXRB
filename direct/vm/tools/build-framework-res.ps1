<#
  Builds the runtime's framework resource package (package id 0x01): the
  themes, styles, layouts, drawables and colors that applications reference
  as @android:... and ?android:attr/..., written for this runtime and compiled
  with aapt2.

  Applications are compiled against the SDK's android.jar, so their binary XML
  and resource tables contain the platform's PUBLIC resource ids (for example
  android:attr/layout_width = 0x010100f4). This package must use exactly those
  ids. Both the id list and the attribute declarations (formats, enum and flag
  values) are derived at build time from the public resource table of the
  pinned platform jar -- API metadata, not framework content:

    public-ids.txt   every PUBLIC resource name -> id (aapt2 --stable-ids)
    attrs_public.xml a <attr> declaration for every PUBLIC attribute
    ids_public.xml   an <item type="id"> for every PUBLIC id

  Everything else (values, styles, layouts, drawables) is the hand-written
  source under src/managed/framework-res.

  Output: <OutDir>/arun-framework-res.apk and <GenDir>/android/R.java.
  The SDK is a BUILD-time dependency only (tools/toolchain.json).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$OutDir,
  [Parameter(Mandatory)][string]$GenDir,
  [string]$BuildTools = '',
  [string]$Platform = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$cfg  = Get-Content (Join-Path $repo 'tools/toolchain.json') -Raw | ConvertFrom-Json
if (-not $BuildTools) { $BuildTools = $cfg.fixture_toolchain.build_tools }
if (-not $Platform) { $Platform = $cfg.fixture_toolchain.public_ids_platform }

function Invoke-Native {
  param([Parameter(Mandatory)][string]$Exe, [object[]]$Arguments, [string]$What)
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $prev }
  if ($code -ne 0) { throw ("{0} failed (exit {1})" -f $What, $code) }
}

$sdk = Join-Path $env:LOCALAPPDATA 'Android/Sdk'
$aapt2 = Join-Path $sdk "build-tools/$BuildTools/aapt2.exe"
$jar = Join-Path $sdk "platforms/$Platform/android.jar"
foreach ($p in @($aapt2, $jar)) { if (-not (Test-Path $p)) { throw "missing SDK component: $p" } }

$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
$GenDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($GenDir)
$src = Join-Path $repo 'managed/framework-res'
$cache = Join-Path $OutDir "public-api-$Platform"
New-Item -ItemType Directory -Force $OutDir, $GenDir, $cache | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false

# --- public ids and attribute declarations (cached per platform jar) -----------
$idsFile = Join-Path $cache 'public-ids.txt'
$attrsFile = Join-Path $cache 'attrs_public.xml'
$stamp = Join-Path $cache 'jar.sha256'
$jarHash = (Get-FileHash $jar -Algorithm SHA256).Hash + ' v3'
if (-not (Test-Path $idsFile) -or -not (Test-Path $attrsFile) -or -not (Test-Path $stamp) -or
    (Get-Content $stamp -Raw).Trim() -ne $jarHash) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  $dump = & $aapt2 dump resources $jar
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prev
  if ($code -ne 0) { throw "aapt2 dump resources failed (exit $code)" }

  $ids = New-Object System.Collections.Generic.List[string]
  # Newer platforms place some (flagged) resources in a second type chunk with
  # another type id (e.g. string/safety_protection_display_text = 0x01dc0005).
  # aapt2 supports one id per type, so only the first type id of a type is kept.
  $typeIds = @{}
  $attrs = New-Object System.Text.StringBuilder
  [void]$attrs.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
  [void]$attrs.AppendLine("<!-- Generated from the public resource table of $Platform android.jar. -->")
  [void]$attrs.AppendLine('<resources>')
  $formats = @('reference','string','integer','boolean','color','float','dimension','fraction')
  $javaKeywords = @('abstract','assert','boolean','break','byte','case','catch','char','class','const',
    'continue','default','do','double','else','enum','extends','final','finally','float','for','goto',
    'if','implements','import','instanceof','int','interface','long','native','new','package','private',
    'protected','public','return','short','static','strictfp','super','switch','synchronized','this',
    'throw','throws','transient','try','void','volatile','while','true','false','null')
  $i = 0
  while ($i -lt $dump.Count) {
    $line = $dump[$i]
    $m = [regex]::Match($line, '^\s+resource (0x[0-9a-f]{8}) ([a-z-]+)/([^ ]+) PUBLIC$')
    $i++
    if (-not $m.Success) { continue }
    $id = $m.Groups[1].Value; $type = $m.Groups[2].Value; $name = $m.Groups[3].Value
    $typeByte = $id.Substring(4, 2)
    if (-not $typeIds.ContainsKey($type)) { $typeIds[$type] = $typeByte }
    if ($typeIds[$type] -ne $typeByte) { continue }
    $ids.Add("android:$type/$name = $id")
    if ($type -ne 'attr') { continue }
    $t = [regex]::Match($dump[$i], '\(attr\) type=([a-z|]+)')
    if (-not $t.Success) { continue }
    $kinds = $t.Groups[1].Value.Split('|')
    $symbols = New-Object System.Collections.Generic.List[string]
    $j = $i + 1
    while ($j -lt $dump.Count) {
      $s = [regex]::Match($dump[$j], '^\s+([A-Za-z0-9_]+)\(0x[0-9a-f]{8}\)=0x([0-9a-f]{8})$')
      if (-not $s.Success) { break }
      $symbols.Add($s.Groups[1].Value + '=' + $s.Groups[2].Value)
      $j++
    }
    $i = $j
    $fmt = ($kinds | Where-Object { $formats -contains $_ }) -join '|'
    $fmtAttr = if ($fmt) { " format=""$fmt""" } else { '' }
    if ($symbols.Count -eq 0) {
      [void]$attrs.AppendLine("  <attr name=""$name""$fmtAttr />")
      continue
    }
    $tag = if ($kinds -contains 'flags') { 'flag' } else { 'enum' }
    [void]$attrs.AppendLine("  <attr name=""$name""$fmtAttr>")
    foreach ($sym in $symbols) {
      $parts = $sym.Split('=')
      # aapt2 turns every enum/flag symbol into an id and R.java field; a symbol
      # named after a Java keyword (e.g. "default") cannot be one. Framework XML
      # written for this runtime uses numeric values for those.
      if ($javaKeywords -contains $parts[0]) { continue }
      $value = [Convert]::ToUInt32($parts[1], 16)
      $text = if ($tag -eq 'enum') { [string][int32]([BitConverter]::ToInt32([BitConverter]::GetBytes($value), 0)) } else { '0x' + $parts[1] }
      [void]$attrs.AppendLine("    <$tag name=""$($parts[0])"" value=""$text"" />")
    }
    [void]$attrs.AppendLine('  </attr>')
  }
  [void]$attrs.AppendLine('</resources>')
  [IO.File]::WriteAllLines($idsFile, $ids.ToArray(), $utf8)
  [IO.File]::WriteAllText($attrsFile, $attrs.ToString(), $utf8)
  [IO.File]::WriteAllText($stamp, $jarHash, $utf8)
  Write-Output ("public API of {0}: {1} resources" -f $Platform, $ids.Count)
}

# --- compile and link ------------------------------------------------------------
$work = Join-Path $OutDir 'fwres-obj'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null
$compiled = Join-Path $work 'compiled'
New-Item -ItemType Directory -Force $compiled | Out-Null

# The generated attribute declarations are compiled as if they were one more
# values file of the source tree.
$genRes = Join-Path $work 'gen-res/values'
New-Item -ItemType Directory -Force $genRes | Out-Null
Copy-Item $attrsFile (Join-Path $genRes 'attrs_public.xml')

# Public ids (android.R.id.*) carry no value; declare every one of them.
$idLines = New-Object System.Text.StringBuilder
[void]$idLines.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
[void]$idLines.AppendLine('<resources>')
foreach ($line in [IO.File]::ReadAllLines($idsFile)) {
  $m = [regex]::Match($line, '^android:id/([A-Za-z0-9_]+) = ')
  if ($m.Success) { [void]$idLines.AppendLine("  <item type=""id"" name=""$($m.Groups[1].Value)"" />") }
}
[void]$idLines.AppendLine('</resources>')
[IO.File]::WriteAllText((Join-Path $genRes 'ids_public.xml'), $idLines.ToString(), $utf8)

Invoke-Native $aapt2 -What 'aapt2 compile (framework res)' -Arguments @(
  'compile', '--dir', (Join-Path $src 'res'), '-o', (Join-Path $work 'res.zip'))
foreach ($f in @('attrs_public.xml', 'ids_public.xml')) {
  Invoke-Native $aapt2 -What "aapt2 compile ($f)" -Arguments @(
    'compile', (Join-Path $genRes $f), '-o', $compiled)
}

$flats = @(Get-ChildItem -Filter '*.flat' $compiled | ForEach-Object { $_.FullName })
$apk = Join-Path $OutDir 'arun-framework-res.apk'
if (Test-Path (Join-Path $GenDir 'android')) { Remove-Item -Recurse -Force (Join-Path $GenDir 'android') }
Invoke-Native $aapt2 -What 'aapt2 link (framework res)' -Arguments (@(
  'link', '-x', '--no-auto-version', '--no-version-vectors',
  '--stable-ids', $idsFile,
  '--manifest', (Join-Path $src 'AndroidManifest.xml'),
  '--java', $GenDir,
  '-o', $apk, (Join-Path $work 'res.zip')) + $flats)

Write-Output ("framework resources: {0} ({1} bytes)" -f $apk, (Get-Item $apk).Length)
