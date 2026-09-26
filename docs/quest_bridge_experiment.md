# Quest Bridge experiment

Branch: `experiment/quest-bridge`. The normal **Play** action still uses Android.

## Try it

1. From the checkout, build:
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/build/quest_bridge.ps1
   ```
   This uses Visual Studio 2022 and the newest installed NDK under the development
   SDK. `-Ndk <directory>` or `ANDROID_NDK_HOME` selects another NDK;
   `-Generator "Visual Studio 18 2026"` selects VS 2026. No SDK is downloaded.
2. Start the development launcher normally. This experiment uses the existing
   launcher setup and its SDK build-tools; first-run Android setup is still required.
3. Import or download a game with all its split APKs and expansion/content files.
   In its details, open **Game actions → Test with Quest Bridge (experimental)**.
   Android installation is not required for this action. It uses the local APK,
   not a possibly different version installed in Android. Prefer the original APK:
   emulator-specific patched libraries may not suit this runtime.
4. Read **Debug** for preparation, unresolved APIs, translator and graphics errors.
   Use **Stop** to request lifecycle shutdown. Closing the launcher requests this too.
5. Use normal **Play** to compare with the emulator. Experimental and Android saves
   are separate. Do not assume they have synchronized.

## What is automated

- Read the package, exact version code and manifest metadata with `aapt2`.
- Verify downloaded content hashes and reject splits from a different package/version.
- Validate ZIP paths, CRCs, extraction limits and duplicate archive entries using
  the launcher's existing archive implementation; check available extraction space.
- Merge ARM64 libraries and assets from base/split APKs. Identical duplicate files
  are accepted; conflicting content fails instead of depending on extraction order.
- Inspect defined ELF64 exports to discover Unity's `JNI_OnLoad` or a unique native
  activity. Manifest `android.app.lib_name` takes precedence. Ambiguous native
  entries, custom entry functions and Java-only apps fail with a diagnostic.
- Discover Unreal's UE4 or UE5 activity namespace from exported functions; the
  inherited Unreal startup shim supplies OBB paths and manifest metadata. This is
  shared engine support, not verified compatibility for every engine version.
- Place APKs, OBBs and external assets in Android-shaped paths without editing APKs.
- Cache prepared content by SHA-256, including split/asset changes and guest zlib.
  Repeated launches verify inputs but skip extraction. New builds preserve saves.
- Keep JIT enabled and strip inherited `QB_*` debugging/patch switches. Use the
  registered desktop OpenXR runtime, or an explicit `XR_RUNTIME_JSON` override.
- Send native focus-loss/pause/stop/destroy or Unity focus-loss/pause/done on Stop.
  After 30 seconds, terminate only this session's runner and report unconfirmed saves.

Preparation lives under the launcher profile's
`quest-bridge/<package>/builds/<content-hash>`; private and external saves live in
`quest-bridge/<package>/saves`. Junctions keep them stable across content updates
and are repaired when a portable profile moves. Cached builds are retained for
comparison; close the runner before manually removing an old build directory.
Keep the `saves` directory. Android Uninstall does not remove these experimental saves.

## Benefits and current gaps

| Existing behavior | Experimental backend |
| --- | --- |
| Library, artwork, Quest account, owned downloads, DLC, APK/ZIP/headset imports | Reuses the launcher; no second account or download implementation |
| Split APKs, OBBs, external assets | Prepared automatically; arbitrary Java asset-pack APIs remain unsupported |
| Original game files | Read without patching or overwriting |
| Diagnostics, launch errors, one active session | Uses existing output capture, error jobs and session history |
| Save-aware Stop | Sends lifecycle callbacks; actual save durability still needs game testing |
| Existing Android saves and permissions | Remain with Android; no migration or permission emulation |
| Vulkan/OpenXR GPU rendering, tracking, controllers | Supplied direct host forwarding path; requires compatible drivers/runtime; not headset-validated here |
| AXRB compositor, layer precomposition, FPS HUD, mirror, SteamVR identity, controller/hand policies | Not carried over: this runner bypasses AXRB's host compositor |
| GLES rendering | Application runner only supplies GLES queries; the separate demo renderer is not general game support |
| Audio | Supplied OpenSL implementation is silent; existing Android audio routing cannot be reused directly |
| In-game Meta identity, entitlements and purchases | Signed-out platform responses in this integration; launcher tokens never passed to games |
| Android Java/Dex/framework services | Partial native/JNI shims, not a Java VM or Android OS |
| Portable release distribution | Development experiment only; no packaged runtime/release integration |

The native process runs with the launcher's Windows permissions. Its Android path
mapping and interpreter are not an OS sandbox. Do not treat it as an Android
security boundary. Unsupported calls can still fail late because much of the
supplied compatibility layer is a prototype.

## Validation and performance

The native suite runs NEON, libc, linking, TLS, threads, EGL bookkeeping, JNI and
NDK native-app-glue probes in both JIT and interpreter modes. It also compares
5,000 randomized instruction candidates (4,074 compared, 926 skipped in this run;
zero differences). This establishes limited instruction/shim behavior, not game
compatibility. The original glue probe had a stale 2528-pixel expectation; it now
checks the implementation's existing 1280×720 window.

`node --test launcher/tests/quest-bridge.test.mjs` checks malformed ELF inputs,
manifest/engine selection, split conflicts, caching, persistent saves, credential
and debug-switch filtering, and launch/Stop through the actual native runner when
it is built. A dedicated ARM64 lifecycle probe verifies pause → stop → destroy.
The inherited app-glue startup probe exits its app thread itself, so sending it a
later pause hangs; this also exercised the bounded forced-stop path.

The real-APK integration check also builds a binary Android manifest with `aapt2`,
prepares its ARM64 library, verifies typed metadata and runs it from a Unicode
profile path. All 17 native tests and the launcher regression suite passed; the
launcher renderer builds with Vite.

No commercial-game headset run or controlled Android/Berberis comparison was
performed. The zip's claimed performance advantage is **unverified**. Compare
the same game version, scene, headset refresh rate and render resolution; record
startup time, CPU/GPU frame times, dropped frames, audio and save/relaunch behavior.
JIT/interpreter instruction throughput alone cannot establish VR performance.

The most valuable next shared improvements are real audio output, framework/JNI
coverage driven by missing-API reports, graphics capability negotiation, and reuse
of AXRB's compositor facilities. Fix engine/API behavior once instead of adding
package names or guest instruction-address patches.
