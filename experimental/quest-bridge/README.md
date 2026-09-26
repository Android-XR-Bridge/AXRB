# Quest Bridge

Runs a Quest game you already own on the PC headset behind the active OpenXR runtime.

`qb-host` is the Windows half. It opens that runtime and reads the head pose and both grip poses. On this machine the active runtime is Meta Horizon Link (`Oculus OpenXR`). SteamVR works the same way when it is the active runtime.

`qb-source` draws both eyes into textures `qb-host` created and shared. `qb-host` copies those textures into the OpenXR swapchains on the GPU. With no source connected, `qb-host` clears a flat color instead.

`qb_runtime.dll` is the OpenXR runtime a game binds to. `qb-app` is that game: it loads the runtime through `XR_RUNTIME_JSON`, renders a grid into the shared textures, and `qb-host` presents them. Start `qb-host` first, then `qb-app`.

`libqb_runtime.so` is the same negotiation entry point built for arm64 Android. It reports a Quest 3.

`libgrid.so` is an Android arm64 program and `qb-guest.exe` is the Windows process that executes it. `guest/cpu.cpp` interprets the arm64 code; an import lands in a thunk page and comes back into this process, which is bound to `qb_runtime.dll`. The loader applies the guest's own relocations, so a pointer the guest holds in its data points where we loaded it. The guest has no libc and no driver of its own, so `guest/runner.cpp` answers `memset` and the OpenGL ES calls it makes. A guest framebuffer names a swapchain texture, and `glDrawArrays` is one D3D11 pass into that texture with the uniforms the guest set. `guest/glsl.cpp` translates the guest's GLSL ES 3.00 into HLSL and `glLinkProgram` compiles it, so the shader that runs is the one the guest wrote. Uniforms get a constant register each through `packoffset`, `mod` keeps GLSL's sign, and `gl_FragCoord.y` is put back on the bottom of the target. A `sampler2D` becomes a texture and a sampler state on the same slot, and a `texture()` call is rewritten to name both. `glTexImage2D` uploads with the rows reversed, because the guest's first row is the bottom of the picture and D3D's is the top. Vertex attributes and matrices are still refused by name, and that refusal is the row that belongs in `compat.md`.

`protocol/bridge.h` is what the Android-side Quest runtime will speak. A frame message names the shared images. The image stays on the GPU.

The game launches as the entitled copy from your Meta account or from your own headset. Meta identity, saves, entitlements, and purchases are answered by that account when that piece is built.

## Build

From a terminal that can see Visual Studio 2026:

```
cmake -G "Visual Studio 18 2026" -A x64 -S . -B build
cmake --build build --config Release
build\Release\qb-host.exe --frames 300
```

`--frames N` exits after N presented frames. With no argument the host runs until the headset session ends.

## Where this sits

The host is piece 4, and it already reads the poses piece 6 needs from the PC side. The next piece is the OpenXR runtime inside the arm64 Android environment, the one Quest games bind to instead of Horizon. It sends `QbFrame` and receives `QbPose`. `compat.md` stays empty until a real game misses a call.
