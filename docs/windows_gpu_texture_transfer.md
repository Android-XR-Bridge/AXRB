# Windows shared GPU eye textures

The Windows Vulkan path can send GPU textures from the Android Emulator to
AXRB/SteamVR without reading or transmitting eye pixels through CPU memory.
The emulator and game still render on the Nvidia GPU. This is GPU-only copying,
not literally zero copies: shared textures are copied into a host-owned GPU
cache and then into the OpenXR swapchain.

## Build and launch

Build the ARM64 runtime and Windows host as usual, then build the small layer:

```powershell
.\host\gpu\build.ps1
.\scripts\run\run_windows_game.ps1 -Package com.example.game -Activity com.example.game/.MainActivity
```

The layer build uses Vulkan headers from the installed Android NDK; it does not
need a Gfxstream fork or a Vulkan SDK installation. `AXRB_VULKAN_HEADERS` can
select another headers directory when configuring CMake directly.

The game launcher enables sharing when the layer manifest exists and it starts
a new emulator. The emulator must be restarted to change its Vulkan layers.
For explicit pixel-transfer mode on a fresh emulator:

```powershell
.\scripts\run\run_windows_game.ps1 -GpuSharing:$false
```

For manual emulator startup use `windows_android_emulator.ps1 -Action Start
-Abi arm64-v8a -GpuSharing`. Install the rebuilt runtime with
`adb install --no-incremental --force-queryable -r`, then `adb shell sync`.
The startup script scopes `VK_LAYER_PATH` and `VK_INSTANCE_LAYERS` to the
emulator process; it does not register a system-wide Vulkan layer or replace
SDK DLLs. The layer manifest uses an absolute DLL path because the emulator's
loader rejects a relative path.

## Frame ownership and synchronization

1. AXRB records a 64-byte marker with `vkCmdUpdateBuffer` before the two eye
   blits. Gfxstream forwards these ordinary Vulkan commands to the host.
2. The host Vulkan layer recognizes the marker, allocates named D3D11 shared
   textures on the same adapter (matched by device LUID), imports their memory
   into Vulkan, and records the eye copies. Vulkan releases queue ownership to
   the external D3D11 consumer. A status marker reports successful recording.
3. The guest waits for its Vulkan fence and checks only the 64-byte status.
   It sends AXRI v3 metadata: a 64-byte header, 96-byte stereo projection, and
   16-byte texture identifier/formats. No image pixels are in that message.
4. The Windows host image-receive thread opens both named textures and copies
   them into its private GPU cache, independently of the OpenXR frame loop.
   Reception uses a separate D3D11 device/context on the same adapter so that
   SteamVR cannot hold up reception while pacing its own graphics context. A D3D11 event query confirms GPU completion before it acknowledges
   the frame sequence. Only then can the guest overwrite the shared textures.
5. A mutex publishes cached pixels together with their projection metadata.
   The cache itself has an NT shared handle, opened on the OpenXR binding device.
   The host frame loop copies it into the current OpenXR swapchain image and
   waits for a GPU event before releasing the mutex, allowing the receive
   device to overwrite the cache safely. Both sides flush and check GPU
   completion; no pixel readback is involved. Retaining
   that cache keeps previously submitted pixels and their render poses together
   while Android produces the next frame. The desktop mirror uses the same GPU
   image.

For a single-layer frame, an absent/rejected host export or failed acknowledgment
disables GPU export for that Android session and falls back to AXRI v2 pixels.
After uncertain completion, the guest never reuses the shared texture pair.
The layer retains allocations until Vulkan device teardown. Resolution/format
changes that do not match the exported pair also fall back rather than corrupting
a frame. Mixed-layer frames require shared-GPU transport; they fail rather than
silently dropping layers into a single-frame CPU fallback.

## Projection reference spaces

Stereo projection metadata preserves whether its render cameras are in host
tracking/world space or canonical OpenXR `VIEW` space. A guest `VIEW` reference
space's offset is folded into the eye poses without baking in a sampled world
head pose. The host submits those projections in its own `VIEW` space, so the
compositor can keep the content head-locked at presentation time. Untagged
projections keep the existing world-space behavior.

The 96-byte projection envelope is unchanged. Bits 0–2 of `layer_flags` remain
OpenXR core composition flags; bit 3 is a projection-only transport `VIEW` tag
and is stripped before calling OpenXR. Unknown bits and the tag on quad or
equirect metadata are rejected. Older hosts reject tagged projections rather
than silently treating their coordinates as world-space; update the Android
runtime and Windows host together. Both pixel and shared-GPU transport preserve
the tag. The capture tool reports it separately as `view_space`.

Native mixed composition keeps each projection in its own `VIEW` or world space,
with independent stereo render cameras, and keeps quad/equirect layers in world
space. When the runtime's layer limit requires GPU flattening, or the optional
[projection compatibility policy](../launcher/README.md#projection-layer-compatibility)
selects it, AXRB transforms private copies of `VIEW` projection poses into host
world space at predicted display time. The output uses the runtime's actual
stereo poses and optical FOV, including for all-`VIEW` stacks. Precomposition
requires valid host views and, for `VIEW` sources, a valid VIEW-to-world transform.
Cached source frames are recomposited when those target poses, FOVs or transforms
change. Flattened layers cannot retain independent late reprojection.

Native spatial and wire-transport regressions cover head-motion invariance,
offset reference spaces and strict flag validation. A local D3D11 pixel smoke
verified world-overlay motion over a head-relative scene. Simulator coverage also
checks wider rotated projections, all-`VIEW` head locking and mixed-space motion.

## Signed vertical projection FOV

Core OpenXR permits `angleDown > angleUp` to request a vertically reversed view.
The guest normalizes these angles independently for each eye before validating
the canonical wire metadata. The reversal is XORed with
`XR_FB_composition_layer_image_layout`'s vertical-flip flag, so two flips cancel.
The existing pixel, single-GPU and batch-GPU paths receive the effective flip;
Vulkan blits reverse the selected crop's source endpoints, not the entire image.
Projection poses and the `VIEW` tag are unaffected.

No wire-layout or Windows host change is needed for this normalization. OVRPort
uses core reversed FOV to preserve positive vertical VrApi texture scales with
Vulkan's top-left image origin; negative scales use ordinary FOV ordering.

The native runtime fixture covers mixed per-eye FOVs, flip cancellation and
invalid angles. The on-device `axrb_vulkan_smoke` target runs actual projection
preparation and Vulkan readback against asymmetric, off-center cropped images,
including shared images and separate array layers. It verifies decoded pixel
orientation through the CPU transport representation; this scenario does not
exercise shared-GPU export or a live headset.

## Ordered projection stacks

Atomic GPU batches preserve application order across any combination of stereo
projection, quad and equirect layers. Projection parts are legal at every batch
index, including multiple consecutive projections and a projection after a quad.
Every part is validated before export, and a complete batch receives one
acknowledgment after all host GPU copies finish.

Native presentation retains each projection's FOV, eye poses, reference space,
blend flags and pixel extent. Projection destinations occupy two adjacent array
slices; quad/equirect destinations occupy one. A projection whose eyes share the
same source image may use one shared GPU texture while retaining two independent
render cameras and two output eyes.

Precomposition samples projections by camera-ray orientation and FOV.
Without depth metadata it cannot reconstruct positional reprojection, so it does
not place the images on an arbitrary-depth quad. Out-of-FOV samples leave the
underlying layers untouched; opaque, premultiplied and unpremultiplied layers
retain their ordered blending semantics. The output always uses the current host
stereo cameras and the acquired projection swapchain's extent, not the first
source layer's camera or image size. This preserves visible coverage from wider
or rotated later projections while retaining the independent-reprojection
limitation described above.

Wire layouts and version numbers are unchanged. Older hosts reject projection
parts after index zero; deploy the Android runtime and Windows host together.
CPU mixed-layer transport remains unsupported.

Validation includes native runtime and TCP acknowledgment regressions, real D3D11
pixel tests for stereo and mono sources, camera rotation, FOV coverage and alpha,
and the Android Vulkan batch probe's dynamic layer counts and rejection handling.
A local host smoke exercised ordered native submissions, interleaved mono/stereo
slices, equirect fallback, mixed-space overflow cache invalidation, empty frames
and procedural test imagery using real D3D11 textures and test OpenXR swapchains.
Native multi-projection presentation remains runtime-dependent; use the optional
compatibility policy when a native stack renders incorrectly. Successful layer
transport alone does not establish full game compatibility.
