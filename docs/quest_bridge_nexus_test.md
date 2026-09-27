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

## Follow-up: layer translation and Vulkan validation

Further testing on the same date reproduced the frame failure with an APK-only
startup harness. The full-content preparation was blocked by its approximately
17 GiB space requirement (16.3 GiB free before these follow-up artifacts). These
APK-only runs intentionally lack the expansion content and are not gameplay tests.

Frame diagnostics identified seven layers: one projection, four equirectangular
panoramas and two quads. SteamVR did not advertise equirectangular layers. The
panoramas/quads also carried vertical-flip and color-scale/bias structures. These
are visible composition features, so simply dropping those layers is not a fix.

The new [shared GPU compositor](quest_bridge_compositor.md) converts panoramas
to stereo projection layers, applies panorama/quad image transforms and selects
the runtime's actual color formats. Synthetic GPU and frame-lifecycle tests
pass under Vulkan validation. The final Nexus startup attempt created several
converted stereo swapchains, but a complete translated game frame was not
verified: other layers had no valid released-image snapshot.

Vulkan validation also exposed a separate problem: Nexus/OVRPlugin requests
zero-sized Application SpaceWarp motion-vector/depth swapchains, followed by
incompatible image views and synchronization errors. The bridge now rejects
the zero-sized requests before forwarding them to the driver. Nexus still
continues with invalid handles after those failures and does not recover cleanly.
This identifies a concrete next investigation; it does not prove that all
remaining faults have the same cause.

The launcher-patched APK was tested as well. Its preparation initially failed
because `liboverport.config.so` is JSON, not ELF; preparation now preserves such
files and excludes them from executable entry-point detection. That APK contains
`disable_space_warp: 1`, but its APK-only bridge run still requested zero-sized
motion-vector images. Preserving a patcher's configuration file does not establish
that the native bridge implements the patcher's runtime behavior.

Follow-up logs are the `nexus-original-2026-09-26T19-*`,
`nexus-original-2026-09-26T20-*` and `nexus-patched-*` files under
`out/quest-bridge`. `compositor-validation.txt` records the synthetic GPU test;
`nexus-compositor-build.txt` and `nexus-tweak-launcher-tests.txt` record regression
checks. All 214 launcher tests and 19 native CTests pass after the changes.
The temporary APK-only prepared builds and downloaded SDK archive were removed
after testing; diagnostic logs, the extracted validation layer and experimental
save folders were retained.

Next: rerun with all expansion content and a connected headset, then resolve the
unsupported SpaceWarp setup and remaining image/synchronization errors.
No playable launch, headset image or performance improvement is claimed.
The existing emulator remains the supported path for this title.

## Full-content retry: 2026-09-27

After disk cleanup, preparation succeeded with the original APK and all 34
external content files. The isolated prepared build is retained for the next
run; it no longer needs to be copied again. Original game files and Android
saves were not changed.

The 90-second run used the updated compositor with Vulkan synchronization
validation enabled. SteamVR initially failed instance creation while starting,
then `xrCreateInstance` succeeded. `xrGetSystem` returned
`XR_ERROR_FORM_FACTOR_UNAVAILABLE`, and SteamVR's client log explicitly recorded
`VRInitError_Init_HmdNotFound`. No OpenXR session or frame submission was reached
in this attempt, so it cannot validate the compositor changes or establish a
rendering regression. Unity continued reporting null-reference exceptions and
attachment-clear/synchronization validation errors without an XR system.
The harness stopped the process using its bounded forced-exit fallback.

Evidence: `out/quest-bridge/nexus-original-2026-09-27T11-54-01-936Z.log` and its
JSON record, plus `nexus-full-content-validation.txt`. A headset recognized by
SteamVR is required for the next rendering test.
