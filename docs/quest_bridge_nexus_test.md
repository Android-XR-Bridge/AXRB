# Assassin's Creed Nexus / Quest Bridge test

Tested locally on 2026-09-26 using `com.Ubisoft.ACNexusVR`, build `207706`
(`MAIN.450412.207706.final`), original APK and all 34 library content files
(16,765,989,965 bytes of external content). The emulator-patched APK was not
used. SteamVR was running and registered as the active OpenXR runtime.

**Result: reaches an OpenXR session and eye swapchain creation, but not a
confirmed playable launch. Frame submission fails and the Vulkan device is lost.**

The harness called the same `prepareBridge()` and `Runtime.launch()` methods
used by the launcher menu. It used `out/quest-bridge/game-tests` for preparation
and fresh experimental saves. The launcher library, original APK/content and
Android saves were not changed. Each attempt had a 90-second observation limit,
followed by lifecycle Stop and its bounded forced-exit fallback.

## Attempts and improvements

1. Original integration: Unity 2021.3.12f1 and IL2CPP initialized, the Oculus
   libraries loaded, and Vulkan instance/device/window creation succeeded.
   Unity reported zero cores and a negative memory size, then its crash handler
   reported SIGILL at `libunity.so+0x219fc8`. Disassembly there is an ordinary
   `str xzr, [x0, #0x50]`; this alone does not establish an unsupported instruction.
   The guest crash handler's register dump is not reliable enough to identify
   the underlying fault.
2. Added generic procfs/sysfs device files, matching the native shim's six-core,
   8 GiB device. On retest Unity correctly reported six cores and 8192 MB, got
   further into initialization, and exited because its Oculus device check saw
   an empty manufacturer. The prototype kept calling `nativeRender` after exit,
   flooding its log. The runner now honors a false return and limits timing
   reports to once per second.
3. Added the shared `props.txt` device profile expected by the existing property
   and JNI shims, including manufacturer, model, ARM64 ABI and Android version.
   Nexus then reached OpenXR extension discovery, successful `xrCreateInstance`,
   `xrGetSystem`, session creation and eye swapchains. OVRPlugin reported
   compositor initialization success and session progression to SYNCHRONIZED.
   Frame submission returned `XR_ERROR_RUNTIME_FAILURE`; Unity attempted an XR
   restart, which did not recover within the observation period.

SteamVR's `xrclient_qb-test.txt` recorded **unsupported layer composition type**,
`xrEndFrame: compositor failed EndFrame()` and repeated
`vkGetFenceStatus failed with -4` (`VK_ERROR_DEVICE_LOST`). The game log also
contains zero-size render-texture errors and a host-call pointer fault during
the XR restart. These identify the rendering/interop path as the next area to
investigate, not proof of one specific root cause. Some optional libraries and
JNI methods are still missing, including a reported unhandled `SetIntField`.

No gameplay, visible headset image, reliable FPS, audio, input or save/relaunch
behavior was verified. Timing lines from initialization and failed submission
must not be treated as gameplay benchmarks. No Android/Berberis comparison was
performed. The user could not check the headset during this test, so its
awake/visible state was not independently confirmed.

## Evidence and follow-up

Local logs and JSON records are under `out/quest-bridge/nexus-original-*`.
The bounded local harness is `out/quest-bridge/test-nexus.mjs`; game files and
logs are intentionally not committed. A snapshot of the SteamVR client log is
saved as `out/quest-bridge/nexus-steamvr-client.txt`.
The temporary prepared game build was removed after testing to recover about
17 GB of disk space; logs, test records and experimental save folders remain.

The fixes are shared runtime preparation/Unity lifecycle behavior, with no Nexus
package special cases or instruction patches. Regression checks cover the
generated device files and refreshing an existing cache; a new ARM64 JNI probe
checks that Unity's render-exit return stops further calls.

Validation after the changes: all 214 launcher tests and all 18 native CTests
passed. The native build also completed successfully.

Next: capture submitted OpenXR layer structures and Vulkan validation output to
isolate the device-loss/unsupported-layer failure before attempting a performance
comparison. The existing emulator remains the supported path for this title.
