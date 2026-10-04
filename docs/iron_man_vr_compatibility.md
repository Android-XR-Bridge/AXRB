# Iron Man VR compatibility

This work addresses startup of the unmodified Quest Android edition of Marvel's Iron Man VR, package `com.camouflaj.salmon`. It does not apply to the PSVR edition.

## Observed working prerequisites

- A hardware-accelerated Android guest with ARM64 native translation and hardware Vulkan rendering.
- Quest-compatible Android identity: Unity's older Oculus hardware check requires the manufacturer to contain `oculus`. Changing only the PC OpenXR runtime does not satisfy this check.
- A legacy `com.oculus.systemdriver.DriverLoader` entry point forwarding to the matching project's ARM64 OpenXR runtime. Function resolution must fall back to `xrGetInstanceProcAddr`, including loader initialization functions that are not ELF exports.
- The Meta Platform initialization interface expected by the game's bundled loader. Refract already provides a compatibility package and its asynchronous internal Unity initialization entry point. AXRB users need that platform-service prerequisite separately. This change does not grant game ownership or provide authenticated Meta services.
- Controller-only extension exposure for the affected older Oculus plugin. During our test, controller movement and buttons worked after the workaround and a restart with controllers connected. Isolated causal proof of the extension filter versus controller startup order remains incomplete.
- Working direct GPU image transport. Hardware rendering alone does not guarantee fast frame transfer. A failed first image connection must not permanently disable sharing before any consumer has connected; failed completion after connection must retain the safety fallback.

## Evidence and limits

The installed Quest build 0.2.1-443305 reached stereo rendering, both controller movement and buttons, and saved progress on September 30, 2026. The test used Windows 11, a hardware-accelerated Android guest and an RTX 3070. VDXR and SteamVR both reached Iron Man rendering, but loading, latency and gameplay performance remained inconsistent. This is startup compatibility evidence, not a claim of full or performant game support.

The optional 80 percent internal render target, sRGB color experiment, and WHPX clock-helper experiment are not startup requirements. They are outside this compatibility change.

## Contribution status

Draft: portable packaging, regression checks and project-specific integration are being prepared from the tested local compatibility components. Existing installations and user data are not part of the contribution. A fresh headset validation of the final portable builds is still required.
