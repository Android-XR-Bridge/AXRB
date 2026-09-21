# Graph Report - AXRB-BS  (2026-09-21)

## Corpus Check
- 256 files · ~313,256 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 19 file(s) not represented in the graph (top: .xml 8, (none) 4, .cmd 1)

## Summary
- 2768 nodes · 5457 edges · 195 communities (139 shown, 56 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 480 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- OpenXR Action & Space Types
- Launcher Archive & Emulator Setup
- Launcher Artwork & Diagnostics
- Windows Host Standard Includes
- Video Transport Sockets
- OpenXR Input Action Handling
- Launcher Permissions Handling
- Launcher Main Process Bootstrap
- Launcher Meta Auth & Downloads
- Audio PCM Recovery & Descriptors
- OpenXR Reference Spaces
- Vulkan/OpenXR Instance Creation
- Direct3D 11 Texture Handling
- Launcher Game Compatibility Checks
- Vulkan Instance/Device Layer Hooks
- Launcher Portable Mode
- Launcher Download & Game Files
- Android Runtime Standard Includes
- Android EGL/JNI Surface Bridge
- GPU Interop Probe Includes
- Android JNI Connection Bridge
- Pose Frame Controller Data
- GPU Frame Transport
- Launcher Game Grid UI
- Launcher Dialog UI Components
- Vulkan Command Buffer Backend
- D3D11 OpenXR View Texture
- Input Action Binding Data
- Android Surface Swapchain
- D3D11 GPU Handle Texture
- Launcher package.json Metadata
- Unreal Memory Policy Script
- OpenXR Session Frame Lifecycle
- Android Content Provider Bridge
- Android Socket I/O
- Android Runtime Policy Script
- Frame Transport Client
- APK Inspection Script
- Vulkan Descriptor Profiling
- Vulkan Command Allocation & Blit
- OpenXR Loader Probe (EGL)
- GPU Frame Descriptor Format
- Android Loader Probe Activity
- GPU Completion Fence
- Frame Interval Tracking
- FPS Counter
- Android Vulkan Layer
- Test Harness Python Includes
- Vulkan Image Memory Formats
- Windows Clock Hook
- FPS HUD Overlay
- Equirect Render Target
- Image Transport TCP Sockets
- Protocol/Runtime Build Targets
- Frame Pool Management
- Project Docs & Backlog
- Launcher Live Diagnostics Panel
- GPU Batch Frame Header
- Vulkan Backend Swapchain
- Audio Policy Tests
- Android Runtime Broker Provider
- OpenXR Function Dispatch
- D3D11 Quad Renderer
- Controller Input & Menu Shortcut
- shadcn/ui Components Config
- Image Frame Version Validation
- Vulkan Instance Probe
- Vulkan Layer Interception
- OpenXR Session Image Receiving
- Host Image Frame Delivery
- D3D11 Pipeline State Objects
- Launcher Select UI Components
- Vulkan Descriptor Template Hooks
- Encoded Video Packet Header
- OpenXR Frame Loop
- Vulkan Device Batch Slots
- D3D11/Vulkan Interop Layer
- GPU Export Frame Metadata
- Equirect Compositor Debug Capture
- Electron Builder Config
- Swapchain Record State
- Vulkan Descriptor Layer Smoke Test
- Android GL Pose Client Init
- Windows OpenXR Host Entry Point
- OpenXR Session Lifecycle
- Vulkan Descriptor Update Template
- GPU Validation Scripts
- SteamVR App Identity Script
- Android Surface Smoke Test
- OpenXR Hand Tracking
- Android Surface Smoke (Java)
- Android SurfaceTexture Frame Signal
- Windows Launcher Entry Point
- GPU Frame Batch
- Layer Compositor View Transform
- OpenXR Action State Getters
- Vulkan Command Buffer Fields
- OpenXR Presentation & View Config
- Launcher Runtime State Machine
- Emulator Watchdog Monitor
- EGL Context Management
- OpenXR Device Selection
- Guest Storage Policy Tests
- OpenXR Action Set Creation
- Launcher Ovrport CLI Integration
- Launcher UI Dependencies
- Windows GPU Marker Header
- ARM64 Atomic Probe
- APK Entry Point Tests
- Vulkan Device Creation
- OpenXR Session Initialization
- Splash Screen Rendering
- Launcher npm Scripts
- Hand Joint Skeleton Data
- Pose Quaternion Fields
- OpenXR Space Record
- Vulkan Export Request
- Android Frame Capture Script
- Launcher Library Actions Smoke Test
- GPU Adapter Matching Tests
- GPU Adapter Tests
- Runtime Architecture Concepts
- Android Surface Header Includes
- COM Smart Pointer (ComPtr)
- Uploaded Composition Part
- Quest Catalog (Java)
- Launcher jsconfig Settings
- Launcher Start Script (CJS)
- Projection View FOV Fields
- Image Quad Layer Fields
- JNI Environment Attachment
- Android Loader Init Info
- OpenXR Time Conversion
- Vulkan Image Barrier Copy
- Portable Helper Tests
- ARM64 Translation Tests
- Android Storage Benchmark
- Launcher Button Component
- Vulkan Scratch Image Allocation
- Vulkan Device Proc Interception
- Vulkan Debug Object Naming
- JNI Math Probe
- Memory Policy Tests
- Loader Probe Docs
- Renderer Smoke Test Targets
- Release Checksum Tests
- Third-Party Runtime Dependencies
- OpenXR Loader License
- GPU Batch Acknowledgement
- Frame Layer Sequence
- AXRP Pose Protocol Versions
- JsonCpp License
- shadcn/ui License
- Game Compatibility & Ovrport
- Launcher UI Shell & CSP
- APK Build Script (sh)
- Loader Probe APK Build
- OpenXR Loader Probe APK Build
- Root Project Definition
- Build Options Toggles
- ARM64 Translation Backlog
- Menu Shortcut Binding
- Clock Filter Test
- Clock Helper Launcher
- Descriptor Template Smoke Test
- GPU Interop Probe
- Windows Launcher Packaging
- Live Diagnostics Panel
- Portable ZIP Constraints
- Electron Runtime Dependency
- Oculus GraphQL API Research
- Quest App Version Switcher
- AXRB Logo
- AXRB vs Quest OS Boundaries
- Launcher Feature Summary
- Repository Layout
- Android/Windows Runtime Split
- GPU Completion Smoke Test
- Splash Smoke Test
- GPU Transport Smoke Test

## God Nodes (most connected - your core abstractions)
1. `OpenXrSession` - 129 edges
2. `VulkanBackend` - 57 edges
3. `log_call()` - 54 edges
4. `PoseFrame` - 47 edges
5. `bootstrap()` - 45 edges
6. `cn()` - 37 edges
7. `AndroidSurface` - 35 edges
8. `is_valid_session()` - 32 edges
9. `MirrorWindow` - 30 edges
10. `Runtime` - 28 edges

## Surprising Connections (you probably didn't know these)
- `main()` --indirect_call--> `command()`  [INFERRED]
  scripts/emulator/android_runtime_policy.py → tests/integration/test_unreal_memory_policy.py
- `main()` --calls--> `vkCreateDevice()`  [INFERRED]
  host/gpu/interop_probe.cpp → runtime/vulkan/android_vulkan_layer.cpp
- `main()` --calls--> `vkDestroyDevice()`  [INFERRED]
  host/gpu/interop_probe.cpp → runtime/vulkan/android_vulkan_layer.cpp
- `main()` --calls--> `destroyInstance()`  [INFERRED]
  tests/native/runtime_smoke.cpp → host/gpu/layer.cpp
- `main()` --calls--> `render`  [INFERRED]
  tests/native/equirect_renderer_smoke.cpp → host/src/equirect_renderer.h

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **GPU Shared-Texture Transport Pipeline** — docs_windows_gpu_texture_transfer_overview, host_gpu_cmakelists_axrb_gpu_layer, host_gpu_cmakelists_axrb_capture_shared, host_src_cmakelists_axrb_host_bridge [INFERRED 0.85]
- **Guest Clock TSC Correction Workflow** — docs_windows_guest_clock_tsc_correction, host_clock_cmakelists_axrb_whpx_clock, docs_windows_nvidia_emulator_setup [INFERRED 0.80]
- **Third-Party License Attribution Bundle** — launcher_third_party_notices_khronos_openxr_loader, launcher_third_party_notices_jsoncpp, launcher_licenses_apache_2_0_license, launcher_licenses_jsoncpp_license [EXTRACTED 0.90]
- **axrb_protocol as shared transport library consumed across runtime and test targets** — protocol_cmakelists_axrb_protocol_target, runtime_src_cmakelists_openxr_runtime_target, tests_cmakelists_axrb_runtime_input_smoke, tests_cmakelists_axrb_native_test_function [INFERRED 0.85]
- **AXRB_RUNTIME_SOURCES propagated from runtime/src to test and smoke targets** — runtime_src_cmakelists_axrb_runtime_sources_var, runtime_src_cmakelists_axrb_vulkan_smoke_target, tests_cmakelists_axrb_runtime_input_smoke [EXTRACTED 1.00]
- **Android runtime/OpenXR loader probes validated alongside the hello_xr Vulkan cube sample** — tests_android_android_loader_probe_readme_direct_runtime_loader_probe, tests_android_android_openxr_loader_probe_readme_openxr_broker_discovery_probe, tests_android_hello_xr_readme_hello_xr_cube_sample [INFERRED 0.80]

## Communities (195 total, 56 thin omitted)

### Community 0 - "OpenXR Action & Space Types"
Cohesion: 0.02
Nodes (87): XrAction, XrActionSet, XrHandTrackerEXT, XrInstance, XrPath, XrReferenceSpaceType, XrSession, XrSpace (+79 more)

### Community 1 - "Launcher Archive & Emulator Setup"
Cohesion: 0.08
Nodes (42): archivePath(), extractZip(), zipSize(), avdConfig(), avdDirectory(), containsPath(), exists(), hardwareRequirementsMet() (+34 more)

### Community 2 - "Launcher Artwork & Diagnostics"
Cohesion: 0.07
Nodes (31): loadLibraryArtwork(), collectDiagnostics(), condense(), DEFAULT_DIAGNOSTICS_ENDPOINT, defaultRedactionPatterns, DIAGNOSTICS_RETENTION_DAYS, diagnosticSources(), diagnosticTailOffset() (+23 more)

### Community 3 - "Windows Host Standard Includes"
Cohesion: 0.09
Nodes (25): atomic, charconv, chrono, client, d3d11, d3d11_1, d3d11_4, dxgi1_2 (+17 more)

### Community 4 - "Video Transport Sockets"
Cohesion: 0.07
Nodes (40): netdb, close_socket(), FrameCallback, sockaddr_in, SocketHandle, string_view, vector, EncodedVideoFrame (+32 more)

### Community 5 - "OpenXR Input Action Handling"
Cohesion: 0.10
Nodes (43): XrAction, XrActionSet, XrInstance, XrPath, XrResult, XrSession, xrApplyHapticFeedback_impl(), xrAttachSessionActionSets_impl() (+35 more)

### Community 6 - "Launcher Permissions Handling"
Cohesion: 0.12
Nodes (20): describePermissionFailure(), LABELS, parsePermissionPrompt(), parseRuntimePermissions(), permissionLabel(), validPermission(), parseInstalledIdentity(), powershellArgs() (+12 more)

### Community 7 - "Launcher Main Process Bootstrap"
Cohesion: 0.09
Nodes (42): bootstrap(), changed(), configuredCli(), controllers, directory, downloadGame(), exclusive(), exists() (+34 more)

### Community 8 - "Launcher Meta Auth & Downloads"
Cohesion: 0.09
Nodes (16): appId(), card(), MetaAuth, nodes(), post(), questApp(), QuestStore, login() (+8 more)

### Community 9 - "Audio PCM Recovery & Descriptors"
Cohesion: 0.06
Nodes (37): Kind, Prepare, write_with_recovery(), pcm_writei(), DescriptorScratchPool, depth, slots, DescriptorTemplateLayout (+29 more)

### Community 10 - "OpenXR Reference Spaces"
Cohesion: 0.12
Nodes (38): xrLocateHandJointsEXT_impl(), XrReferenceSpaceType, XrResult, XrSession, XrSpace, XrTime, XrView, xrCreateActionSpace_impl() (+30 more)

### Community 11 - "Vulkan/OpenXR Instance Creation"
Cohesion: 0.13
Nodes (39): VkDevice, VkInstance, VkPhysicalDevice, VkResult, XrInstance, XrResult, XrSystemId, xrCreateInstance_impl() (+31 more)

### Community 12 - "Direct3D 11 Texture Handling"
Cohesion: 0.06
Nodes (39): DXGI_FORMAT, HWND, ID3D11Device, ID3D11DeviceContext, ID3D11Texture2D, string, UINT, DXGI_FORMAT (+31 more)

### Community 13 - "Launcher Game Compatibility Checks"
Cohesion: 0.09
Nodes (23): assertKnownKeys(), checkPatchName(), checkProfile(), checkVersionRule(), checkVersionValue(), compatibilityRuntimeOptions(), formatVerifiedVersions(), formatVersionRule() (+15 more)

### Community 14 - "Vulkan Instance/Device Layer Hooks"
Cohesion: 0.06
Nodes (38): PFN_vkGetInstanceProcAddr, VkAllocationCallbacks, VkDevice, VkDeviceCreateInfo, VkInstance, VkInstanceCreateInfo, VkPhysicalDevice, XrStructureType (+30 more)

### Community 15 - "Launcher Portable Mode"
Cohesion: 0.14
Nodes (33): compatibilityPatchArgs(), carryPortableFiles(), configurePortable(), inside(), portableOutput(), resolvedDestination(), sweepPortableTemp(), openWindowsFeatures() (+25 more)

### Community 16 - "Launcher Download & Game Files"
Cohesion: 0.14
Nodes (19): checkSpace(), downloadFile(), fetchFile(), safeName(), assetDestination(), classifyAsset(), fileRecord(), importGameZip() (+11 more)

### Community 17 - "Android Runtime Standard Includes"
Cohesion: 0.10
Nodes (21): cerrno, cmath, cstddef, ctime, dlfcn, array, limits, log (+13 more)

### Community 18 - "Android EGL/JNI Surface Bridge"
Cohesion: 0.06
Nodes (29): EGLImageKHR, jfloatArray, jmethodID, AndroidSurface, consume_, context_, dirty_, display_ (+21 more)

### Community 19 - "GPU Interop Probe Includes"
Cohesion: 0.11
Nodes (14): algorithm, cstdio, cstdlib, cstring, fcntl, vector, quad_renderer, resource (+6 more)

### Community 20 - "Android JNI Connection Bridge"
Cohesion: 0.09
Nodes (29): jni, poll, android_context(), connect_with_timeout(), JavaVM, jobject, sockaddr_in, JavaVM (+21 more)

### Community 21 - "Pose Frame Controller Data"
Cohesion: 0.06
Nodes (32): PoseFrame, aim, aim_active, aim_flags, controllers, display_period_ns, grip_flags, hand_tracking_supported (+24 more)

### Community 22 - "GPU Frame Transport"
Cohesion: 0.11
Nodes (21): cstdint, functional, FrameCallback, vector, recv_descriptor_with_fds(), send_gpu_frame_descriptor(), UniqueFd, fd_ (+13 more)

### Community 23 - "Launcher Game Grid UI"
Cohesion: 0.19
Nodes (23): activeStatuses, bytes(), call(), Empty(), EMULATOR_DOTS, EMULATOR_LABELS, EmulatorStatus(), GameGrid() (+15 more)

### Community 24 - "Launcher Dialog UI Components"
Cohesion: 0.12
Nodes (21): Cover(), safeImage(), Dialog(), DialogContent(), DialogDescription(), DialogFooter(), DialogHeader(), DialogOverlay() (+13 more)

### Community 25 - "Vulkan Command Buffer Backend"
Cohesion: 0.06
Nodes (29): vector, VkBuffer, VkCommandBuffer, VkCommandPool, VkDeviceSize, VulkanBackend, batchFailed_, batchInFlight_ (+21 more)

### Community 26 - "D3D11 OpenXR View Texture"
Cohesion: 0.07
Nodes (29): DXGI_FORMAT, ID3D11Device, ID3D11DeviceContext, ID3D11Texture2D, XrExtent2Di, XrView, EquirectRenderer, blend_ (+21 more)

### Community 27 - "Input Action Binding Data"
Cohesion: 0.07
Nodes (29): InputValue, active, x, y, ActionRecord, bindings, magic, samples (+21 more)

### Community 28 - "Android Surface Swapchain"
Cohesion: 0.14
Nodes (27): jobject, XrResult, XrSession, XrSwapchain, XrSwapchainCreateInfo, xrCreateSwapchainAndroidSurfaceKHR_impl(), xrDestroySession_impl(), XrSwapchain (+19 more)

### Community 29 - "D3D11 GPU Handle Texture"
Cohesion: 0.11
Nodes (21): DXGI_FORMAT, HANDLE, ID3D11Device, ID3D11DeviceContext, ID3D11Texture2D, UINT, WindowsGpuReceiver, cached_ (+13 more)

### Community 30 - "Launcher package.json Metadata"
Cohesion: 0.08
Nodes (25): author, description, devDependencies, electron, electron-builder, tailwindcss, @tailwindcss/vite, vite (+17 more)

### Community 31 - "Unreal Memory Policy Script"
Cohesion: 0.13
Nodes (24): ctypes, os, apply(), read(), root(), shell(), configure(), describe_adapters() (+16 more)

### Community 32 - "OpenXR Session Frame Lifecycle"
Cohesion: 0.09
Nodes (25): XrResult, monotonic_time_ns(), beginFrame_, createReferenceSpace_, current_xr_time, endFrame_, latest_frame, locate_controller_spaces (+17 more)

### Community 33 - "Android Content Provider Bridge"
Cohesion: 0.15
Nodes (14): android.content.ContentProvider, android.content.ContentValues, android.database.Cursor, android.net.Uri, file, inetsocketaddress, java.io.DataInputStream, java.net.Socket (+6 more)

### Community 34 - "Android Socket I/O"
Cohesion: 0.14
Nodes (9): android.net.LocalSocket, datagrampacket, inputstream, ioexception, java.net.DatagramSocket, java.net.InetAddress, localserversocket, ImageProxy (+1 more)

### Community 35 - "Android Runtime Policy Script"
Cohesion: 0.16
Nodes (18): hashlib, ensure_adb_root(), main(), run(), Apply emulator configuration without opening or rewriting application files., apply_audio_policy(), shell(), audio_ready() (+10 more)

### Community 36 - "Frame Transport Client"
Cohesion: 0.14
Nodes (17): vector, XrFrameEndInfo, XrResult, XrTime, ImageTransportClient, directWindows_, lastConnectAttemptNs_, lastError_ (+9 more)

### Community 37 - "APK Inspection Script"
Cohesion: 0.12
Nodes (19): argparse, base64, json, _dump(), inspect(), _manifest_activity(), Read APK identity with Android SDK tools; never install or modify the APK., Find an enabled public MAIN entry point omitted by aapt's badging view. (+11 more)

### Community 38 - "Vulkan Descriptor Profiling"
Cohesion: 0.09
Nodes (19): clockid_t, time_point, VkDescriptorSet, DescriptorProfile, active, expanded, lookedUp, start (+11 more)

### Community 39 - "Vulkan Command Allocation & Blit"
Cohesion: 0.14
Nodes (23): allocateCommands(), beginCommand(), blitImage(), VkAllocationCallbacks, VKAPI_ATTR, VkCommandBuffer, VkCommandPool, VkDevice (+15 more)

### Community 40 - "OpenXR Loader Probe (EGL)"
Cohesion: 0.13
Nodes (23): openxr_platform, sstream, EGLContext, EGLDisplay, EGLSurface, Function, jclass, JNIEnv (+15 more)

### Community 41 - "GPU Frame Descriptor Format"
Cohesion: 0.09
Nodes (23): GpuFrameDescriptor, drm_format, drm_modifier, fd_count, format_type, header_size, height, layers (+15 more)

### Community 42 - "Android Loader Probe Activity"
Cohesion: 0.15
Nodes (12): android.app.Activity, android.os.Bundle, fileoutputstream, gravity, standardcharsets, Override, MainActivity, Override (+4 more)

### Community 43 - "GPU Completion Fence"
Cohesion: 0.12
Nodes (17): GpuCompletion, context_, context4_, device_, event_, failed_, fence_, query_ (+9 more)

### Community 44 - "Frame Interval Tracking"
Cohesion: 0.11
Nodes (17): FrameIntervals, previous_, started_, stats_, mutex, time_point, vector, PerfScope (+9 more)

### Community 45 - "FPS Counter"
Cohesion: 0.11
Nodes (17): FpsCounter, previous_, started_, time_, time_point, MenuShortcut, holding_, startedNs_ (+9 more)

### Community 46 - "Android Vulkan Layer"
Cohesion: 0.17
Nodes (20): map, apiVersion(), vector, VkDeviceCreateInfo, VkPhysicalDevice, VkResult, extensions(), has() (+12 more)

### Community 47 - "Test Harness Python Includes"
Cohesion: 0.21
Nodes (11): importlib_util, pathlib, shutil, subprocess, sys, tempfile, Test real Android pose socket with concurrent readers; stop the host first.…, threading (+3 more)

### Community 48 - "Vulkan Image Memory Formats"
Cohesion: 0.11
Nodes (20): array, VkDeviceMemory, VkFormat, VkImage, ScaledImage, format, height, image (+12 more)

### Community 49 - "Windows Clock Hook"
Cohesion: 0.12
Nodes (16): cwchar, AxrbInitializeClock(), DWORD, UINT32, WHV_REGISTER_NAME, WHV_REGISTER_VALUE, set_registers(), main() (+8 more)

### Community 50 - "FPS HUD Overlay"
Cohesion: 0.11
Nodes (16): vector, XrCompositionLayerQuad, hud_pixels(), OpenXrSession::update_fps_hud(), PFN_xrGetInstanceProcAddr, load_func(), OpenXrLoader, createInstance (+8 more)

### Community 51 - "Equirect Render Target"
Cohesion: 0.12
Nodes (19): EquirectTarget, images, layer, swapchain, views, array, XrCompositionLayerEquirect2KHR, XrCompositionLayerQuad (+11 more)

### Community 52 - "Image Transport TCP Sockets"
Cohesion: 0.17
Nodes (15): in, inet, close_socket(), SocketHandle, make_fake_pose_frame(), monotonic_time_ns(), send_all(), SocketRuntime (+7 more)

### Community 53 - "Protocol/Runtime Build Targets"
Cohesion: 0.12
Nodes (19): axrb_protocol static library target, ws2_32 (Windows Sockets) link dependency, AXRB_RUNTIME_SOURCES shared build variable, axrb_vulkan_smoke on-device Vulkan readback regression executable, openxr_runtime shared library target, loader_probe shared library target, openxr_loader imported shared library (libopenxr_loader.so), openxr_loader_probe shared library target (+11 more)

### Community 54 - "Frame Pool Management"
Cohesion: 0.14
Nodes (16): Count, F, FramePool, state_, shared_ptr, T, unique_ptr, Slot (+8 more)

### Community 55 - "Project Docs & Backlog"
Cohesion: 0.12
Nodes (18): Educational and Lawful-Use Notice, Pinball Performance Follow-up, Shared Texture Memory Priority Backlog, Runtime-Selected OpenXR Resolution, Ordered Projection Stacks / Atomic GPU Batches, Windows Shared GPU Eye Texture Transfer, Shared-Texture Snapshot Diagnostics (axrb_capture_shared), Windows Guest Clock TSC Correction Experiment (+10 more)

### Community 57 - "GPU Batch Frame Header"
Cohesion: 0.11
Nodes (18): GpuBatchPart, gpu, header, projection, ImageFrameHeader, bytes_per_pixel, format, header_size (+10 more)

### Community 58 - "Vulkan Backend Swapchain"
Cohesion: 0.24
Nodes (16): vector, VkDeviceSize, VkResult, XrSwapchainSubImage, ok(), begin, ensure_buffer, export_batch (+8 more)

### Community 59 - "Audio Policy Tests"
Cohesion: 0.16
Nodes (3): AudioPolicyTests, Guest, NdkDiscoveryTests

### Community 60 - "Android Runtime Broker Provider"
Cohesion: 0.21
Nodes (3): android.content.pm.ApplicationInfo, Bundle, RuntimeBrokerProvider

### Community 61 - "OpenXR Function Dispatch"
Cohesion: 0.15
Nodes (14): AXRB_XR_EXPORT, PFN_xrVoidFunction, cast_function(), Function, XrInstance, XrResult, negotiate_loader_runtime_interface(), xrGetInstanceProcAddr_impl() (+6 more)

### Community 62 - "D3D11 Quad Renderer"
Cohesion: 0.24
Nodes (16): d3dcompiler, bind_target(), DXGI_FORMAT, ID3D11BlendState, ID3D11Device, ID3D11DeviceContext, ID3D11RasterizerState, ID3D11Texture2D (+8 more)

### Community 63 - "Controller Input & Menu Shortcut"
Cohesion: 0.18
Nodes (10): controller_binding(), string_view, display_period_or_default(), has_valid_view_fovs(), valid_display_period(), valid_view_fov(), PoseStreamDecoder, partial_ (+2 more)

### Community 64 - "shadcn/ui Components Config"
Cohesion: 0.12
Nodes (16): aliases, components, hooks, lib, ui, utils, iconLibrary, rsc (+8 more)

### Community 65 - "Image Frame Version Validation"
Cohesion: 0.18
Nodes (14): equirect_gpu_version(), equirect_version(), mixed_gpu_version(), valid_mixed_part(), close_socket(), FrameCallback, SocketHandle, vector (+6 more)

### Community 66 - "Vulkan Instance Probe"
Cohesion: 0.16
Nodes (16): main(), instance, VkInstance, VkInstanceCreateInfo, vkCreateInstance(), vkDestroyInstance(), vkGetInstanceProcAddr(), main() (+8 more)

### Community 67 - "Vulkan Layer Interception"
Cohesion: 0.22
Nodes (15): PFN_vkGetInstanceProcAddr, PFN_vkVoidFunction, VkInstance, VkInstanceCreateInfo, createInstance(), destroyInstance(), Instance, gipa (+7 more)

### Community 68 - "OpenXR Session Image Receiving"
Cohesion: 0.23
Nodes (13): vector, OpenXrSession::receive_image(), valid_gpu_batch(), ImageProjection, layer_flags, view_count, valid_equirect(), valid_projection() (+5 more)

### Community 69 - "Host Image Frame Delivery"
Cohesion: 0.13
Nodes (14): mutex, shared_ptr, time_point, HostImageFrame, deliveredFrames, latest, mutex, HostImageSnapshot (+6 more)

### Community 70 - "D3D11 Pipeline State Objects"
Cohesion: 0.12
Nodes (16): ID3D11BlendState, ID3D11Buffer, ID3D11PixelShader, ID3D11RasterizerState, ID3D11SamplerState, ID3D11VertexShader, QuadRenderer, blend_ (+8 more)

### Community 71 - "Launcher Select UI Components"
Cohesion: 0.17
Nodes (13): SelectContent(), SelectItem(), SelectLabel(), SelectScrollDownButton(), SelectScrollUpButton(), SelectSeparator(), SelectTrigger(), clampHeight() (+5 more)

### Community 72 - "Vulkan Descriptor Template Hooks"
Cohesion: 0.14
Nodes (16): PFN_vkCreateDescriptorUpdateTemplate, PFN_vkDestroyDescriptorUpdateTemplate, PFN_vkUpdateDescriptorSets, PFN_vkUpdateDescriptorSetWithTemplate, PFN_vkGetDeviceProcAddr, PFN_vkGetInstanceProcAddr, T, Device (+8 more)

### Community 73 - "Encoded Video Packet Header"
Cohesion: 0.12
Nodes (16): EncodedVideoPacketHeader, capture_time_ns, chunk_offset, chunk_size, codec, flags, frame_id, frame_size (+8 more)

### Community 74 - "OpenXR Frame Loop"
Cohesion: 0.26
Nodes (15): XrFrameEndInfo, XrResult, XrSession, xrBeginFrame_impl(), xrEndFrame_impl(), xrEnumerateDisplayRefreshRatesFB_impl(), xrGetDisplayRefreshRateFB_impl(), xrRequestDisplayRefreshRateFB_impl() (+7 more)

### Community 75 - "Vulkan Device Batch Slots"
Cohesion: 0.14
Nodes (15): PFN_vkGetDeviceProcAddr, T, unique_ptr, vector, Device, batchSlots, commands, device (+7 more)

### Community 76 - "D3D11/Vulkan Interop Layer"
Cohesion: 0.16
Nodes (14): ID3D11Device, ID3D11DeviceContext, PFN_vkGetDeviceProcAddr, PFN_vkGetInstanceProcAddr, VkDevice, VkInstance, VkPhysicalDevice, SharedDevice (+6 more)

### Community 77 - "GPU Export Frame Metadata"
Cohesion: 0.14
Nodes (14): Export, eyes, formats, height, width, HANDLE, ID3D11Texture2D, VkDeviceMemory (+6 more)

### Community 78 - "Equirect Compositor Debug Capture"
Cohesion: 0.14
Nodes (12): debug_capture_frame(), ID3D11DeviceContext, ID3D11Texture2D, UINT, ID3D11Texture2D, XrCompositionLayerEquirect2KHR, XrTime, OpenXrSession::render_equirect() (+4 more)

### Community 79 - "Electron Builder Config"
Cohesion: 0.14
Nodes (14): build, appId, beforePack, directories, extraResources, files, portable, productName (+6 more)

### Community 80 - "Swapchain Record State"
Cohesion: 0.14
Nodes (14): SwapchainRecord, acquired, arraySize, created, currentImage, format, hasReleasedImage, height (+6 more)

### Community 81 - "Vulkan Descriptor Layer Smoke Test"
Cohesion: 0.26
Nodes (13): VkAllocationCallbacks, VKAPI_ATTR, VkDescriptorSet, VkDescriptorUpdateTemplate, VkDescriptorUpdateTemplateCreateInfo, VkDevice, VkWriteDescriptorSet, create() (+5 more)

### Community 82 - "Android GL Pose Client Init"
Cohesion: 0.19
Nodes (9): gl2ext, GLenum, initializer_list, PoseClient, AndroidSurface::~AndroidSurface(), initialize, update, compile() (+1 more)

### Community 83 - "Windows OpenXR Host Entry Point"
Cohesion: 0.24
Nodes (10): wchar_t, main(), wmain(), vector, decode_video_frame_to_image(), OpenXrHost, run, parse_u16() (+2 more)

### Community 84 - "OpenXR Session Lifecycle"
Cohesion: 0.26
Nodes (12): XrInstance, XrResult, XrSession, xrBeginSession_impl(), xrCreateSession_impl(), xrEndSession_impl(), xrPollEvent_impl(), queue_session_state() (+4 more)

### Community 85 - "Vulkan Descriptor Update Template"
Cohesion: 0.42
Nodes (13): device, VkAllocationCallbacks, VKAPI_ATTR, VkDescriptorUpdateTemplate, VkDescriptorUpdateTemplateCreateInfo, VkDevice, vkCreateDescriptorUpdateTemplate(), vkCreateDescriptorUpdateTemplateKHR() (+5 more)

### Community 86 - "GPU Validation Scripts"
Cohesion: 0.22
Nodes (7): Assert-AxrbGuestGpu(), Get-ManagedEmulatorProcess(), Invoke-Adb(), Invoke-ExternalWithTimeout(), Stop-StaleManagedEmulator(), Verify-Abi(), Verify-Gpu()

### Community 87 - "SteamVR App Identity Script"
Cohesion: 0.31
Nodes (5): app_key(), main(), manifest_data(), Register APK metadata with SteamVR; associate it with an OpenXR host PID. Uses…, SteamVR

### Community 88 - "Android Surface Smoke Test"
Cohesion: 0.26
Nodes (10): android_surface, pose_client, jclass, JNIEnv, JNIEXPORT, jobject, Java_SurfaceSmoke_create(), Java_SurfaceSmoke_destroy() (+2 more)

### Community 89 - "OpenXR Hand Tracking"
Cohesion: 0.23
Nodes (11): XrHandTrackerEXT, XrResult, XrSession, find_hand_tracker(), xrCreateHandTrackerEXT_impl(), xrDestroyHandTrackerEXT_impl(), HandTrackerRecord, alive (+3 more)

### Community 90 - "Android Surface Smoke (Java)"
Cohesion: 0.29
Nodes (5): android.content.Context, android.view.Surface, canvas, color, SurfaceSmoke

### Community 91 - "Android SurfaceTexture Frame Signal"
Cohesion: 0.22
Nodes (7): android.graphics.SurfaceTexture, atomicboolean, handler, looper, OnFrameAvailableListener, Override, SurfaceFrameSignal

### Community 92 - "Windows Launcher Entry Point"
Cohesion: 0.29
Nodes (10): HMODULE, DWORD, HANDLE, wchar_t, quote(), remote_call(), remote_module(), wmain() (+2 more)

### Community 93 - "GPU Frame Batch"
Cohesion: 0.20
Nodes (9): main(), GpuFrameBatch, count, parts, Part, header, projection, receiver (+1 more)

### Community 94 - "Layer Compositor View Transform"
Cohesion: 0.31
Nodes (9): convert_view_to_world(), ID3D11Texture2D, XrPosef, multiply(), OpenXrSession::compose_overflow(), relative_to_world(), rotate(), XrQuaternionf (+1 more)

### Community 95 - "OpenXR Action State Getters"
Cohesion: 0.20
Nodes (11): XrPosef, getActionStateBoolean_, getActionStateFloat_, getActionStatePose_, getActionStateVector2f_, locateHandJoints_, syncActions_, to_protocol_pose() (+3 more)

### Community 96 - "Vulkan Command Buffer Fields"
Cohesion: 0.22
Nodes (10): Command, buffer, eye, family, marker, offset, pool, VkBuffer (+2 more)

### Community 97 - "OpenXR Presentation & View Config"
Cohesion: 0.33
Nodes (9): enumerateViewConfigurationViews_, ID3D11Texture2D, XrPosef, XrView, OpenXrSession::create_projection_swapchain(), OpenXrSession::fill_projection_texture(), OpenXrSession::upload_android_frame(), same_pose() (+1 more)

### Community 100 - "EGL Context Management"
Cohesion: 0.22
Nodes (9): EGLContext, EGLDisplay, EGLSurface, Current, context, display, draw, read (+1 more)

### Community 101 - "OpenXR Device Selection"
Cohesion: 0.22
Nodes (8): VkInstance, VkPhysicalDevice, XrResult, XrSwapchainCreateInfo, choose_device, create, destroy, main()

### Community 103 - "OpenXR Action Set Creation"
Cohesion: 0.22
Nodes (9): attachSessionActionSets_, createAction_, createActionSet_, createActionSpace_, string_to_path, suggest_pose_bindings, suggestInteractionProfileBindings_, OpenXrSession::initialize_controller_actions() (+1 more)

### Community 104 - "Launcher Ovrport CLI Integration"
Cohesion: 0.33
Nodes (3): Ovrport, selectedPatchArgs(), catalog

### Community 105 - "Launcher UI Dependencies"
Cohesion: 0.22
Nodes (9): dependencies, class-variance-authority, clsx, lucide-react, radix-ui, react, react-dom, tailwind-merge (+1 more)

### Community 106 - "Windows GPU Marker Header"
Cohesion: 0.22
Nodes (9): WindowsGpuMarker, formats, height, magic, reserved, sequence, session, status (+1 more)

### Community 107 - "ARM64 Atomic Probe"
Cohesion: 0.31
Nodes (8): add32(), jclass, jint, JNIEnv, JNIEXPORT, Java_com_axrb_mathprobe_MainActivity_run(), swap32(), swap64()

### Community 109 - "Vulkan Device Creation"
Cohesion: 0.25
Nodes (7): VkDeviceCreateInfo, VkPhysicalDevice, createDevice(), UINT, VkFormat, wchar_t, gdpa

### Community 110 - "OpenXR Session Initialization"
Cohesion: 0.25
Nodes (8): string, create_reference_space, createSession_, getSystem_, initialize, initialize_controller_actions, initialize_hand_tracking, load_instance_functions

### Community 111 - "Splash Screen Rendering"
Cohesion: 0.25
Nodes (5): vector, load_splash_pixels(), splash, main(), wincodec

### Community 112 - "Launcher npm Scripts"
Cohesion: 0.25
Nodes (8): scripts, build, build:quest, dist, dist:portable, smoke, start, test

### Community 113 - "Hand Joint Skeleton Data"
Cohesion: 0.25
Nodes (8): HandJoint, flags, pose, radius, HandSkeleton, active, joints, source

### Community 114 - "Pose Quaternion Fields"
Cohesion: 0.25
Nodes (8): Pose, qw, qx, qy, qz, x, y, z

### Community 115 - "OpenXR Space Record"
Cohesion: 0.25
Nodes (8): SpaceKind, XrPosef, RuntimeHandle, magic, SpaceRecord, handle, kind, offsetInParent

### Community 116 - "Vulkan Export Request"
Cohesion: 0.25
Nodes (8): XrSwapchainSubImage, VulkanExportRequest, height, indices, subimages, swapchains, verticalFlip, width

### Community 117 - "Android Frame Capture Script"
Cohesion: 0.36
Nodes (7): struct, chunk(), main(), Capture one AXRB TCP RGBA frame as PNG, without a headset or dependencies., read_exact(), write_png(), zlib

### Community 118 - "Launcher Library Actions Smoke Test"
Cohesion: 0.43
Nodes (5): libraryActionsSmoke(), check(), choosePatch(), closeDetails(), openDetails()

### Community 121 - "Runtime Architecture Concepts"
Cohesion: 0.33
Nodes (6): Windows Hand Tracking Relay, Frame Ownership and GPU Synchronization, Projection Reference Spaces (VIEW vs World), axrb_gpu_layer (Vulkan Shared-Texture Layer), axrb_gpu_receiver_probe, axrb-host-bridge (Windows OpenXR Host Executable)

### Community 122 - "Android Surface Header Includes"
Cohesion: 0.33
Nodes (5): egl, eglext, gl3, hardware_buffer, PoseClient

### Community 123 - "COM Smart Pointer (ComPtr)"
Cohesion: 0.47
Nodes (3): ComPtr, ptr_, T

### Community 124 - "Uploaded Composition Part"
Cohesion: 0.33
Nodes (6): XrExtent2Di, UploadedCompositionPart, extent, imageArrayIndex, panel, projection

### Community 125 - "Quest Catalog (Java)"
Cohesion: 0.33
Nodes (4): jsonarray, jsonobject, QuestCatalog, packagemanager

### Community 126 - "Launcher jsconfig Settings"
Cohesion: 0.33
Nodes (5): compilerOptions, baseUrl, jsx, paths, include

### Community 127 - "Launcher Start Script (CJS)"
Cohesion: 0.33
Nodes (5): applicationArgs, build, environment, forwarded, { spawn }

### Community 128 - "Projection View FOV Fields"
Cohesion: 0.33
Nodes (6): ImageProjectionView, angle_down, angle_left, angle_right, angle_up, pose

### Community 129 - "Image Quad Layer Fields"
Cohesion: 0.33
Nodes (6): ImageQuad, eye_visibility, height, layer_flags, pose, width

### Community 130 - "JNI Environment Attachment"
Cohesion: 0.40
Nodes (6): JavaVM, JNIEnv, Env, attached, env, vm

### Community 131 - "Android Loader Init Info"
Cohesion: 0.33
Nodes (6): AndroidLoaderInitInfo, applicationContext, applicationVM, next, type, XrStructureType

### Community 132 - "OpenXR Time Conversion"
Cohesion: 0.53
Nodes (6): XrInstance, XrResult, XrTime, xrConvertTimespecTimeToTimeKHR_impl(), xrConvertTimeToTimespecTimeKHR_impl(), timespec

### Community 133 - "Vulkan Image Barrier Copy"
Cohesion: 0.33
Nodes (6): VkImage, VkImageLayout, barrier, VulkanBackend::copy_surface(), VkAccessFlags, VkImageAspectFlags

### Community 135 - "ARM64 Translation Tests"
Cohesion: 0.53
Nodes (4): build(), main(), Build unchanged ARM64 atomic tests and a small benchmark; run via NativeBridge.…, run()

### Community 136 - "Android Storage Benchmark"
Cohesion: 0.53
Nodes (5): main(), measured_shell(), shell(), Compare read-ahead on existing assets; run with games stopped. Only reads the…, run()

### Community 137 - "Launcher Button Component"
Cohesion: 0.50
Nodes (4): Button(), buttonVariants, class-variance-authority, radix-ui

### Community 138 - "Vulkan Scratch Image Allocation"
Cohesion: 0.50
Nodes (5): VkDeviceMemory, VkFormat, allocate_image, ensure_scratch, VkImageUsageFlags

### Community 139 - "Vulkan Device Proc Interception"
Cohesion: 0.40
Nodes (5): alias(), PFN_vkVoidFunction, gdpa, intercept(), vkGetDeviceProcAddr()

### Community 140 - "Vulkan Debug Object Naming"
Cohesion: 0.40
Nodes (5): brokenDebugNames(), vkSetDebugUtilsObjectNameEXT(), wrappedObject(), VkDebugUtilsObjectNameInfoEXT, VkObjectType

### Community 141 - "JNI Math Probe"
Cohesion: 0.40
Nodes (5): jclass, jint, JNIEnv, JNIEXPORT, Java_com_axrb_mathprobe_MainActivity_run()

### Community 143 - "Loader Probe Docs"
Cohesion: 0.67
Nodes (4): Direct runtime loader probe (doc), OpenXR broker discovery probe (doc), hello_xr Khronos cube sample (doc), Windows NVIDIA emulator setup doc (referenced)

### Community 144 - "Renderer Smoke Test Targets"
Cohesion: 0.50
Nodes (4): axrb_equirect_renderer_smoke test executable, axrb_host_bridge_starts test (runs axrb-host-bridge), axrb-host-bridge executable target (defined outside this chunk), axrb_quad_renderer_smoke test executable

### Community 146 - "Third-Party Runtime Dependencies"
Cohesion: 0.67
Nodes (3): MinHook Dependency (Pinned Revision), LLVM-NDK License Text (GPLv2 Content), Packaged Runtime Dependencies (Python, MinHook, LLVM/libc++)

### Community 147 - "OpenXR Loader License"
Cohesion: 0.67
Nodes (3): Pinned Khronos OpenXR Loader Download, Apache License 2.0 Text, Khronos OpenXR Loader Redistribution

### Community 149 - "Frame Layer Sequence"
Cohesion: 0.67
Nodes (3): Frame, layers, sequence

## Ambiguous Edges - Review These
- `Packaged Runtime Dependencies (Python, MinHook, LLVM/libc++)` → `LLVM-NDK License Text (GPLv2 Content)`  [AMBIGUOUS]
  launcher/licenses/LLVM-NDK.txt · relation: references

## Knowledge Gaps
- **672 isolated node(s):** `instance`, `eyes`, `width`, `height`, `formats` (+667 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1241 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **56 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `Packaged Runtime Dependencies (Python, MinHook, LLVM/libc++)` and `LLVM-NDK License Text (GPLv2 Content)`?**
  _Edge tagged AMBIGUOUS (relation: references) - confidence is low._
- **Why does `OpenXrSession` connect `OpenXR Action & Space Types` to `OpenXR Session Frame Lifecycle`, `OpenXR Presentation & View Config`, `Windows Host Standard Includes`, `Host Image Frame Delivery`, `OpenXR Action Set Creation`, `FPS Counter`, `OpenXR Session Initialization`, `Equirect Compositor Debug Capture`, `FPS HUD Overlay`, `Equirect Render Target`, `Pose Frame Controller Data`, `OpenXR Action State Getters`?**
  _High betweenness centrality (0.089) - this node is a cross-community bridge._
- **Why does `ComPtr` connect `COM Smart Pointer (ComPtr)` to `Windows Host Standard Includes`, `D3D11 Pipeline State Objects`, `GPU Completion Fence`, `Direct3D 11 Texture Handling`, `D3D11/Vulkan Interop Layer`, `GPU Export Frame Metadata`, `D3D11 OpenXR View Texture`, `D3D11 GPU Handle Texture`?**
  _High betweenness centrality (0.035) - this node is a cross-community bridge._
- **Why does `PoseFrame` connect `Pose Frame Controller Data` to `OpenXR Action & Space Types`, `OpenXR Session Frame Lifecycle`, `OpenXR Reference Spaces`, `FPS Counter`, `Hand Joint Skeleton Data`, `Pose Quaternion Fields`, `Image Transport TCP Sockets`, `Android JNI Connection Bridge`, `Android Surface Smoke Test`, `OpenXR Action State Getters`, `Controller Input & Menu Shortcut`?**
  _High betweenness centrality (0.030) - this node is a cross-community bridge._
- **Are the 53 inferred relationships involving `log_call()` (e.g. with `negotiate_loader_runtime_interface()` and `xrBeginFrame_impl()`) actually correct?**
  _`log_call()` has 53 INFERRED edges - model-reasoned connections that need verification._
- **Are the 2 inferred relationships involving `bootstrap()` (e.g. with `launcher/main.mjs` and `changed()`) actually correct?**
  _`bootstrap()` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `instance`, `eyes`, `width` to the rest of the system?**
  _672 weakly-connected nodes found - possible documentation gaps or missing edges._