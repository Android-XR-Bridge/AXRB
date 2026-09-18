param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../paths.ps1"
function Run([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed ($LASTEXITCODE)" }
}
Push-Location $AxrbRoot
try {
    Run cmake @('--preset', 'windows')
    Run cmake @('--build', '--preset', 'windows', '--parallel')
    foreach ($component in @('gpu', 'clock')) {
        Run cmake @('-S', "host/$component", '-B', "out/$component", '-G', 'Visual Studio 17 2022', '-A', 'x64')
        Run cmake @('--build', "out/$component", '--config', 'Release', '--parallel')
        if (!$SkipTests) { Run ctest @('--test-dir', "out/$component", '-C', 'Release', '--output-on-failure') }
    }
    if (!$SkipTests) { Run ctest @('--preset', 'windows') }
} finally { Pop-Location }
