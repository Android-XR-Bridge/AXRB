# Quest Bridge Vulkan composition fallback

The experimental backend now implements `XR_KHR_composition_layer_equirect2`
when the active runtime does not provide it. The math follows the launcher's
existing `host/src/equirect_renderer.cpp`: each panoramic layer becomes a stereo
projection layer in the original reference space at the frame's display time.
Layer order, angular bounds, finite/infinite radius, orientation, source crop,
array slice, eye visibility and alpha flags are retained.

The adapter also consumes the image-flip and color-scale/bias structures that
OVRPlugin attaches to panoramas and quads, even on runtimes that do not advertise
those extensions. Identity transforms on core projection/quad layers are
removed from a copy. The guest's frame structures are not rewritten. Other
nontrivial layer transforms remain unsupported and return an explicit failure;
the adapter does not silently remove visible layers.

## GPU ownership and formats

- Color images are copied into private GPU images **before** the guest releases
  them. Conversion therefore never reads an OpenXR image after release.
- Compute shaders render the transformed content. A GPU blit converts the
  private linear output into a color format actually advertised by the runtime,
  including sRGB-only runtimes. No CPU pixel readback is used in this path.
- Guest and compositor queue operations share a lock, including the guest's
  accelerated Vulkan dispatch path. Acquired guest images are tracked in FIFO
  order; timeouts are not mistaken for successful waits.
- A conversion error closes the frame with no layers and returns an error to
  the guest. It never reports a failed conversion as successful display.
- Swapchain dimensions are validated before passing them to the host driver.
  Invalid zero-sized motion-vector requests receive `XR_ERROR_VALIDATION_FAILURE`.

This first version intentionally favors correctness over throughput. It adds
color-image snapshots, private textures, GPU blits and fence waits. It has not
been benchmarked against Android/Berberis. It supports single-sample 2D color
swapchains; multiview panoramas are generated as a two-slice output. It does not
implement Application SpaceWarp, HDR composition, or every Meta layer extension.

## Build and verification

`scripts/build/quest_bridge.ps1` compiles `guest/xr_layers.comp` with the selected
NDK's `glslc` and places `xr_layers.spv` beside `qb-test.exe`. Both files are
required at runtime. Vulkan headers are taken from that NDK without including
its Android C library headers in the Windows build.

The `qb-xr-layers` CTest uses real Vulkan compute and blits on synthetic textures,
plus a small fake OpenXR runtime which offers only sRGB. It checks planar copy,
flip, crop, color/alpha transforms, panorama orientation, angular bounds and eye
selection; it also verifies frame order, stereo slices, sRGB conversion,
unchanged guest structures, timeout retry, FIFO acquisition and resource cleanup.
It does **not** establish headset or commercial-game compatibility.

The test passes with Khronos Vulkan validation and synchronization validation
enabled, with no validation errors. The expected diagnostics for intentionally
injected timeout/unknown-extension failures are part of the regression test.

The implementation follows the [OpenXR frame submission and swapchain
contracts](https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html).
The local diagnostic layer was extracted from the
[official Vulkan SDK](https://vulkan.lunarg.com/sdk/home), without a system
installation; it is not a runtime dependency or committed binary.

See [Nexus testing](quest_bridge_nexus_test.md) for the remaining game failures.
