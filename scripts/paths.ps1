$AxrbRoot = Split-Path $PSScriptRoot -Parent
$AxrbOut = Join-Path $AxrbRoot 'out'
$AxrbAssets = $AxrbOut

# These scripts read their Python helpers' stdout as UTF-8 and AXRB can be
# extracted to any path, so pin the interpreter instead of letting it fall
# back to whatever code page the host locale happens to use.
$env:PYTHONUTF8 = '1'

# SteamVR can invoke a packaged script without starting the Electron launcher.
# Derive portable homes from the adjacent marker, not the caller's environment.
$AxrbPortableRoot = $null
$portableCandidate = [IO.Path]::GetFullPath((Join-Path $AxrbRoot '../..'))
if (Test-Path -LiteralPath (Join-Path $portableCandidate 'AXRB.portable') -PathType Leaf) {
    $AxrbPortableRoot = $portableCandidate
    function Assert-AxrbPortablePath([string]$Value) {
        $full = [IO.Path]::GetFullPath($Value)
        $prefix = $AxrbPortableRoot.TrimEnd('\') + '\'
        if ($full -ne $AxrbPortableRoot -and !$full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Portable mode keeps output inside its folder.'
        }
        for ($parent = $full; $parent -ne $AxrbPortableRoot; $parent = Split-Path $parent -Parent) {
            if (Test-Path -LiteralPath $parent) {
                if ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Portable script paths cannot use filesystem redirection: $parent"
                }
            }
        }
        return $full
    }
    $data = Assert-AxrbPortablePath (Join-Path $AxrbPortableRoot 'data')
    $temporary = Assert-AxrbPortablePath (Join-Path $AxrbPortableRoot 'temp')
    $AxrbPortableSettings = $null
    $libraryFile = Join-Path $data 'library.json'
    $library = if (Test-Path -LiteralPath $libraryFile) { Get-Content -LiteralPath $libraryFile -Raw -Encoding UTF8 | ConvertFrom-Json } else { $null }
    if ($library) { $AxrbPortableSettings = $library.settings }
    $managed = Join-Path $AxrbPortableRoot 'AXRB Runtime'
    if ($env:AXRB_PORTABLE_ROOT -eq $AxrbPortableRoot -and $env:AXRB_DATA_HOME) {
        # A setup-in-progress may be preparing a different disk than the saved one.
        $managed = Split-Path (Assert-AxrbPortablePath $env:AXRB_DATA_HOME) -Parent
    } elseif ($AxrbPortableSettings.managedDirectory) {
        $managed = $AxrbPortableSettings.managedDirectory
        if ($library.portableRoot) {
            $oldPrefix = $library.portableRoot.TrimEnd('\', '/') + '\'
            if ($managed.StartsWith($oldPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                $managed = Join-Path $AxrbPortableRoot $managed.Substring($oldPrefix.Length)
            }
        }
    }
    $AxrbManagedDirectory = Assert-AxrbPortablePath $managed
    $env:TEMP = $temporary
    $env:TMP = $temporary
    $env:TMPDIR = $temporary
    $env:AXRB_PORTABLE_ROOT = $AxrbPortableRoot
    $env:AXRB_DATA_HOME = Assert-AxrbPortablePath (Join-Path $managed 'output')
    $env:ANDROID_AVD_HOME = Assert-AxrbPortablePath (Join-Path $managed 'avd')
    $env:ANDROID_USER_HOME = Assert-AxrbPortablePath (Join-Path $managed 'android')
    $env:ANDROID_EMULATOR_HOME = $env:ANDROID_USER_HOME
    $env:ANDROID_SDK_HOME = $data
    $env:ANDROID_HOME = Assert-AxrbPortablePath (Join-Path $managed 'sdk')
    $env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
    New-Item -ItemType Directory -Force -Path $data, $temporary, $env:ANDROID_USER_HOME | Out-Null
    if ($env:AXRB_CAPTURE_PREFIX) {
        $captures = Assert-AxrbPortablePath (Join-Path $data 'captures')
        New-Item -ItemType Directory -Force -Path $captures | Out-Null
        $env:AXRB_CAPTURE_PREFIX = Join-Path $captures 'frame'
    }
    $env:PATH = "$(Join-Path $AxrbRoot 'tools/python');$env:PATH"
    Set-Location -LiteralPath $AxrbPortableRoot
}

if ($env:AXRB_DATA_HOME) { $AxrbOut = $env:AXRB_DATA_HOME }
$AxrbHostExe = Join-Path $AxrbAssets 'host/bin/Release/axrb-host-bridge.exe'
$AxrbGpuDirectory = Join-Path $AxrbAssets 'gpu/Release'
$AxrbClockDirectory = Join-Path $AxrbAssets 'clock/Release'
$AxrbKeys = Join-Path $AxrbRoot '.local/keys'
# A keystore dropped at the checkout root is the signing identity for every APK
# this repository builds. Clean checkouts have none, so fall back to the
# generated debug key under .local; both paths are gitignored.
$AxrbKeystore = Join-Path $AxrbRoot 'runtime.keystore'
if (!(Test-Path -LiteralPath $AxrbKeystore)) { $AxrbKeystore = Join-Path $AxrbKeys 'runtime.keystore' }

if ($env:AXRB_KEYSTORE_PATH) { $AxrbKeystore = $env:AXRB_KEYSTORE_PATH }
