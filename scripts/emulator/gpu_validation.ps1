function Test-AxrbHostGpu($Controller) {
    # Names alone also match disabled cards and virtual display drivers.
    return ($Controller.ConfigManagerErrorCode -eq 0 -and
        $Controller.PNPDeviceID -match '^PCI\\VEN_(1002|10DE)&')
}

function Get-AxrbEmulatorGpuSelection([object[]]$Controllers, [string]$Override) {
    if (![string]::IsNullOrWhiteSpace($Override)) { return $Override }
    $supported = @($Controllers | Where-Object { Test-AxrbHostGpu $_ })
    # Use a name, never a Windows adapter index: Vulkan enumerates independently.
    # Leave multi-AMD/NVIDIA systems to the emulator or an explicit override.
    if ($supported.Count -eq 1 -and ![string]::IsNullOrWhiteSpace($supported[0].Name)) {
        return $supported[0].Name
    }
    return $null
}

function Assert-AxrbGuestGpu([string]$Gles, [object[]]$Devices) {
    $software = 'SwiftShader|llvmpipe|softpipe|software|Microsoft Basic|lavapipe'
    if ($Gles -notmatch '\b(NVIDIA|AMD|ATI|Radeon)\b' -or $Gles -match $software) {
        throw "AMD or NVIDIA hardware GLES required; found: $Gles"
    }
    if (!$Devices.Count) { throw 'Guest reports no Vulkan devices.' }
    foreach ($device in $Devices) {
        $p = $device.properties
        if ($p.vendorID -notin @(0x1002, 0x10de) -or $p.deviceType -notin @(1, 2) -or $p.deviceName -match $software) {
            throw "AMD or NVIDIA hardware Vulkan required; found: $($p.deviceName). Android is exposing an unsupported Vulkan adapter. If a supported GPU is installed, update its driver and restart Android with that GPU selected."
        }
    }
}
