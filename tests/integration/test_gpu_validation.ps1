$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../../scripts/emulator/gpu_validation.ps1"
foreach ($vendor in @('1002', '10DE')) {
    if (!(Test-AxrbHostGpu ([pscustomobject]@{ ConfigManagerErrorCode=0; PNPDeviceID="PCI\VEN_$vendor&DEV_1234" }))) { throw 'Hardware rejected' }
}
foreach ($controller in @(
    @{ConfigManagerErrorCode=22; PNPDeviceID='PCI\VEN_1002&DEV_1234'},
    @{ConfigManagerErrorCode=0; PNPDeviceID='ROOT\DISPLAY'}
)) { if (Test-AxrbHostGpu ([pscustomobject]$controller)) { throw 'Unsupported adapter accepted' } }
foreach ($vendor in @(0x1002, 0x10de)) {
    foreach ($type in @(1,2)) {
        Assert-AxrbGuestGpu 'AMD Radeon / NVIDIA hardware' @(@{properties=@{vendorID=$vendor;deviceType=$type;deviceName='Hardware'}})
    }
}
foreach ($case in @(
    @{gles='SwiftShader';vendor=0x1002;type=2;name='Hardware'},
    @{gles='AMD Radeon';vendor=0x1002;type=4;name='Hardware'},
    @{gles='NVIDIA';vendor=0x10de;type=2;name='llvmpipe'},
    @{gles='AMD';vendor=0x1234;type=2;name='Hardware'}
)) {
    $rejected=$false
    try { Assert-AxrbGuestGpu $case.gles @(@{properties=@{vendorID=$case.vendor;deviceType=$case.type;deviceName=$case.name}}) } catch { $rejected=$true }
    if (!$rejected) { throw 'Invalid guest accepted' }
}
$intel = [pscustomobject]@{ConfigManagerErrorCode=0; PNPDeviceID='PCI\VEN_8086&DEV_1234'; Name='Intel(R) UHD Graphics 770'}
$nvidia = [pscustomobject]@{ConfigManagerErrorCode=0; PNPDeviceID='PCI\VEN_10DE&DEV_1234'; Name='NVIDIA GeForce RTX 3070 Ti'}
$amd = [pscustomobject]@{ConfigManagerErrorCode=0; PNPDeviceID='PCI\VEN_1002&DEV_1234'; Name='AMD Radeon RX 6800'}
foreach ($gpu in @($nvidia, $amd)) {
    foreach ($adapters in @(@($intel, $gpu), @($gpu, $intel))) {
        if ((Get-AxrbEmulatorGpuSelection $adapters '') -ne $gpu.Name) { throw 'Mixed-GPU selection failed' }
    }
}
if ((Get-AxrbEmulatorGpuSelection @($intel, $nvidia) 'custom GPU') -ne 'custom GPU') { throw 'Explicit GPU override lost' }
if ($null -ne (Get-AxrbEmulatorGpuSelection @($intel) '')) { throw 'Intel-only system selected' }
if ($null -ne (Get-AxrbEmulatorGpuSelection @($nvidia, $amd) '')) { throw 'Ambiguous supported GPUs selected' }
$disabled = [pscustomobject]@{ConfigManagerErrorCode=22; PNPDeviceID=$nvidia.PNPDeviceID; Name=$nvidia.Name}
if ($null -ne (Get-AxrbEmulatorGpuSelection @($intel, $disabled) '')) { throw 'Disabled GPU selected' }
Write-Output 'GPU validation tests passed'
