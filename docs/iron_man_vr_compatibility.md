# Iron Man VR compatibility

This work addresses startup of the unmodified Quest Android edition of Marvel's Iron Man VR, package `com.camouflaj.salmon`. It does not apply to the PSVR edition.

## Observed working prerequisites

- The complete Quest game APK and matching expansion assets installed in Android, not just the APK. The tested 0.2.1-443305 download contained an APK plus 33 content files. Saved progress is optional for startup.
- A hardware-accelerated Android guest with ARM64 native translation and hardware Vulkan rendering. On Windows, CPU virtualization and the Windows Hypervisor Platform must be available. ARM64 translation must support the game's Vulkan path; use the translator documented for your project rather than assuming the same translator is required by both forks.
- Quest-compatible Android identity: Unity's older Oculus hardware check requires the manufacturer to contain `oculus`. Changing only the PC OpenXR runtime does not satisfy this check.
- A legacy `com.oculus.systemdriver.DriverLoader` entry point forwarding to the matching project's ARM64 OpenXR runtime. Function resolution must fall back to `xrGetInstanceProcAddr`, including loader initialization functions that are not ELF exports.
- The Meta Platform initialization interface expected by the game's bundled loader. Refract already provides a compatibility package and its asynchronous internal Unity initialization entry point. AXRB users need that platform-service prerequisite separately. This change does not grant game ownership or provide authenticated Meta services.
- Controller-only extension exposure for the affected older Oculus plugin. During our test, controller movement and buttons worked after the workaround and a restart with controllers connected. Isolated causal proof of the extension filter versus controller startup order remains incomplete.
- Working direct GPU image transport. Hardware rendering alone does not guarantee fast frame transfer. A failed first image connection must not permanently disable sharing before any consumer has connected; failed completion after connection must retain the safety fallback.

## Evidence and limits

The installed Quest build 0.2.1-443305 reached stereo rendering, both controller movement and buttons, and saved progress on September 30, 2026. The test used Windows 11, a hardware-accelerated Android guest and an RTX 3070. VDXR and SteamVR both reached Iron Man rendering, but loading, latency and gameplay performance remained inconsistent. This is startup compatibility evidence, not a claim of full or performant game support.

The optional 80 percent internal render target, sRGB color experiment, and WHPX clock-helper experiment are not startup requirements. They are outside this compatibility change.

## Contribution status

Draft: the portable ARM64 runtime/driver packages build and pass signature verification. Missing-export function-lookup regression tests pass, and runtime payload equality is verified. Existing installations and user data are not part of the contribution. A fresh headset validation of the final portable builds is still required.

## Build and install the legacy adapter

Build the ARM64 runtime using the project's normal runtime APK builder (`runtime/apk/build_apk.ps1 -Abi arm64-v8a -Sdk <SDK> -Jdk <JDK>`). The build also produces `out/android/runtime-arm64-v8a/axrb-systemdriver-debug.apk` from the same runtime library. Do not mix a driver APK with an older runtime build.

With the emulator running, use `scripts/run/install_legacy_quest_compat.ps1 -Sdk <SDK> -Port <emulator-port> -ControllersOnly`. It installs the matching runtime and driver APKs and persists a controller-only policy scoped to Iron Man. Omit `-ControllersOnly` and rerun to clear that policy. Stop and restart the game after changing it. This affects extension discovery, not physical tracking scale.

The installer accepts `-PlatformApk <APK>` for an existing compatible platform initialization package, such as Refract's separately built `platform-sdk` package. It does not modify ownership state. Use that package's existing documented lawful-content configuration; this PR does not authenticate Meta cloud services or provide downloads.

Quest identity must be configured separately before game startup. This project provides `scripts/emulator/device_identity.ps1 -Identity quest -Sdk <SDK> -Serial emulator-<port>`; it reboots the guest. After a reboot, confirm the selected hardware renderer and matching runtime packages again. Connect the PC OpenXR headset session and wake both controllers before launching Iron Man.

The controller flag is opt-in and scoped to `persist.sys.axrb.legacy.controllers_only.com.camouflaj.salmon`; other titles keep their advertised hand tracking extensions. This portable packaging needs a fresh headset confirmation before promoting this draft as a general compatibility claim.

The helper selects the project's normal local ADB server (AXRB 5038, Refract 5037). Pass `-AdbServerPort` if your guest uses another local server. The identity helper backs up guest build properties before changing them and addresses emulator serials only. It requires a writable, root-capable Android guest. Stop the game before installing packages or changing policy.

## Direct mode

Direct mode applies the same compatibility without the emulator, scripts or a root-capable guest. The launcher's compatibility profile for Iron Man VR (`launcher/core/game-compatibility.json`, version code 443305) turns on three per-game runtime options:

- `legacyOculusDriver`: the session offers `com.oculus.systemdriver` as an installed package. Its APK (`out/direct/guest/axrb-systemdriver.apk`, staged by `scripts/build/build_direct.ps1`) supplies `DriverLoader` and the adapter library, and the session's own `libopenxr_runtime.so` is loaded through it. Such a game runs its unmodified store APK; it does not need an OVRPort patch.
- `questIdentity`: the session reports manufacturer `Oculus`, brand `oculus` and model `Quest 3`. Other games keep the default identity.
- `controllersOnly`: sets `persist.sys.axrb.legacy.controllers_only.<package>` for the session. The adapter reads it under the game's process name, which it now takes from `Application.getProcessName()` because a direct-mode command line is `app_process64`.

`direct/tools/test_legacy_driver.py` builds a probe app that loads the runtime the way older Oculus loaders do and checks the identity and controller policy in a direct session. It passes; Iron Man VR itself has not been run in direct mode yet. The Meta Platform initialization package is still a separate prerequisite there.
