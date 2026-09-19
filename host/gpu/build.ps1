param(
    [string]$BuildDirectory = "$PSScriptRoot\..\..\out/gpu",
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$VulkanHeaders
)
$ErrorActionPreference = 'Stop'
$arguments = @('-S', $PSScriptRoot, '-B', $BuildDirectory, '-G', $Generator, '-A', 'x64')
if ($VulkanHeaders) {
    $headers = (Resolve-Path -LiteralPath $VulkanHeaders).Path
    $arguments += "-DAXRB_VULKAN_HEADERS=$headers"
}
cmake @arguments
if ($LASTEXITCODE) { throw 'GPU layer configuration failed.' }
cmake --build $BuildDirectory --config Release
if ($LASTEXITCODE) { throw 'GPU layer build failed. Stop the emulator before rebuilding a loaded DLL.' }
& "$BuildDirectory\Release\axrb_gpu_interop_probe.exe"
if ($LASTEXITCODE) { throw 'GPU interop probe failed.' }
