# Refract performance port

Compared against [MIXIDtheSilly/Refract](https://github.com/MIXIDtheSilly/Refract)
at `8a03ae4`. Scope: performance and latency, preserving AXRB's launcher, Meta
integration, entitlement behavior, APK handling, and supported composition layers.
Imported code is covered by `licenses/Refract-MIT.txt`; Digitalis/Berberis keeps
its Apache-2.0 notices.

| Upstream optimization | AXRB disposition |
| --- | --- |
| Digitalis lite load/store shortcut | Excluded after it crashed `Arm64MemoryDifferentialFuzz.TaggedBaseAddressIsIgnored`. Unconditional top-byte masking is retained for correctness. Other patches target pinned translator `9e7f4407`. |
| Direct translated-block chaining (`687fceb`) | Included in the combined Digitalis patch. |
| Host call/return prediction (`487d2f9`) | Included, without references to unpublished diagnostic counters. |
| Gear up small hot regions (`7e77b20`) | `berberis.gearup_min_insns=0` when Digitalis is active. |
| Syslib redirection | Included for Digitalis guest-library compatibility. |
| SIMD-address, heavy-readonly-TBI, pairwise-minmax candidate patches | Retained for review, not enabled: absent from the shipped Refract bundle's active patch list. |
| GPU ASTC-to-BC3 (`35c4d23`) | Default on capable devices. AXRB staging ownership and submit-time source revalidation retained. BC7 remains selectable. |
| ETC2/EAC-to-BC1/3/4/5 (`35c4d23`) | Uses AXRB's existing worker pool, staging reuse and upload lifetime tracking. |
| Asynchronous Vulkan GPU export and pixel readback | Fence waiting and delivery run on a persistent worker. One outstanding publication bounds latency and preserves AXRB's whole-frame ACK ownership. |
| Frame-paced pose publication and guest pacing (`0358bb7`) | Host wakes the pose sender on new tracking samples; Android waits for a fresh pose with bounded timer fallback. |
| Adaptive fresh-frame pickup budget | Tracks host frame misses, reduces waiting after misses and recovers conservatively. `AXRB_FRESH_FRAME_WAIT_US` still overrides it. |
| Pose-tag history / adaptive display-frame lag | Retains AXRB's latest-frame selection and existing optional history. Refract repurposes metadata AXRB uses for whole-frame multi-layer transport; copying that wire format would break AXRB composition. |
| Direct emulator image TCP | Uses `10.0.2.2:38491`, avoiding the adb reverse relay. Non-emulator transport remains available. |
| Growing panel swapchains (`5ca8c44`) | Grow in 256-pixel steps without shrinking while the swapchain is in use. |
| Recoverable `xrEndFrame` errors | Continue the host frame loop; stop it for session/instance loss. |
| Dynamic multi-layer atlas and export retirement | AXRB already has whole-frame batches, dynamic layer counts, mono/stereo transitions, ACK gating and export retirement; retained. |
| D3D11 fence-event waits (`fa5d64d`, `d1e1ab3`) | Already present in AXRB, including fallback coverage. |
| Descriptor layout caching (`006341a`) | Already present; retained cached entry points, scratch reuse and concurrent lifetime handling. |
| Cached Vulkan buffers and Unity skinning acceleration | Existing AXRB implementations retained. |
| Address-space graphics (ASG) | Already the AXRB default. |
| P-core scheduling | Emulator inherits the highest efficiency-class core mask on single-group hybrid systems. Parent affinity is restored. Homogeneous/multi-group systems retain Windows scheduling. |
| Corrected TSC and vCPU handling | Existing AXRB corrected-clock and hypervisor-APIC paths retained. Refract's busy idle polling is not made a default. |
| `NO_TTWU_QUEUE` and missing zram restoration | Applied to the emulator; missing kernel facilities are reported without preventing game launch. Active swap is never reset. |
| DirectSound audio latency (`ee07a0c`) | 20 ms latency and 40 ms output buffer by default, configurable. |
| Unity texture mip reduction | Not forced: this reduces game texture resolution rather than accelerating equivalent rendering. GPU texture transcoding provides the performance path without changing mip selection. |
| Refract launcher, Store removal, IDs, account/Meta behavior, HUD redesign | Excluded from this performance-only port. AXRB features remain in place. |

## Controls

The launcher's **Texture conversion** setting defaults to **Performance (GPU / BC3)**;
**Higher quality (BC7)** applies on the next game launch.
`scripts/run/run_windows_game.ps1 -TextureMode gpu` is the command-line default. Use
`-TextureMode bc7` for higher texture quality. GPU mode uses the compute path
for primary, one-time command buffers on compute-capable queues, and CPU BC3
for other uploads to those images. A device that cannot initialize the compute
pipeline retains BC7. ETC2/EAC conversion is available in both modes.

For direct runtime testing, set `debug.axrb.astc_gpu` to `1` (GPU/BC3) or `0`
(BC7) before starting the process. `debug.axrb.astc_to_bc7=0` disables compressed
texture substitution entirely. `debug.axrb.async_export=0` and
`debug.axrb.frame_sync=0` independently disable asynchronous delivery and pose pacing.
`-AllHostCores` on the emulator script disables P-core selection.
`debug.axrb.image_transport=adb` retains the previous adb-reverse image connection;
the default is direct emulator TCP. A 64-byte echo probe on this machine measured
roughly 0.59–0.62 ms median direct RTT versus 0.59–0.71 ms via adb reverse across
two runs each; this does not establish a universal throughput or game FPS gain.

Digitalis can be selected with `-Translator digitalis`, restored with
`-Translator google`, or installed automatically when a verified bundle is
available. `AXRB_DIGITALIS_BUNDLE` can point to the bundle on another drive.
See [the translator build instructions](../tools/translator/README.md).

## Verification

The port has Windows native tests, Android descriptor-layer tests, real Vulkan
pixel readback and deferred GPU-batch tests, ETC2/EAC reference decoding checks,
and a desktop GPU ASTC reference comparison. `tests/android/texture_gpu_smoke.cpp`
checks real GPU conversion, source writes after recording, and repeated resource
retirement. These checks establish correctness; they are not game FPS benchmarks.

Completed checks on the development machine:

- Windows Release build and 25 native tests passed.
- All 20 launcher runtime-launch tests passed, including default GPU/BC3 and
  explicit BC7 argument forwarding. The production launcher UI build passed.
- Performance-policy, translator-bundle integrity and distribution tests passed
  (4, 4 and 7 tests respectively).
- Both Android runtime APKs (`x86_64` and `arm64-v8a`) and the Vulkan layer built.
- Real Android Vulkan readback, deferred publication, six GPU-batch frames,
  descriptor handling and GPU texture lifetime/source-rewrite checks passed.
- Both texture modes passed through the actual loaded Android Vulkan layer.
  On the solid-block gradient fixture, GPU/BC3 produced 51.37 dB PSNR with a
  maximum channel error of 2; BC7 reproduced the reference exactly. This fixture
  verifies the path and is not a general image-quality or speed benchmark.
- ETC2/EAC reference checks and the desktop GPU ASTC reference comparison passed.

The patched Digitalis ARM64 host suite passed 3,736 tests; 2 hardware-dependent
tests were skipped and 3 upstream tests remain disabled. Both lite and heavy
tagged-pointer differential fuzz tests passed after retaining safe TBI masking.
The complete Digitalis distribution built successfully. All 74 canonical
artifacts passed presence, ELF architecture, native-bridge export and checksum
verification. The AXRB archive includes the pinned source manifest, combined
patch and license notices. The local bundle is on `D:\AXRB\digitalis-prebuilts`,
exposed to this checkout through `out/digitalis/prebuilts`; the archive is
`D:\AXRB\digitalis-prebuilts-axrb.tar.gz`.
