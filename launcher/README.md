# AXRB launcher

A native Windows desktop frontend for the AXRB runtime: your installed Android
games, the live Quest storefront, owned-game downloads, expansion files and DLC.

## Run

Run `AXRB-<version>.exe` to open AXRB directly, or extract the portable ZIP
described below. The EXE has no Windows installation wizard. It keeps the
launcher profile in `%APPDATA%/AXRB` and defaults the Android runtime to
`%LOCALAPPDATA%/AXRB Runtime`.
First-run setup checks Windows Hypervisor Platform, explains how to enable it
if needed, and installs the pinned Android 16 runtime after license acceptance.
Choose a drive and Android disk size; setup checks available space first.
Games and ovrport are not bundled.
This build requires Windows x64, an AMD or NVIDIA GPU, at least 12 GB RAM, and an active
OpenXR runtime such as SteamVR.

For a new installation, **Use downloaded setup files** accepts one or more of
the four Android setup ZIPs (emulator, platform-tools, build-tools, system image).
Files must match the exact sizes and SHA-256/SHA-1 checksums pinned in
`core/components.json`; names do not matter. Unknown, corrupt or duplicate
components are rejected rather than silently downloaded instead. A partial
selection downloads only the remaining missing components. Selected ZIPs are
extracted directly into the private managed SDK, without copying or deleting
the originals or linking an external SDK. Keep them outside files setup replaces.
Selections are not saved between launches and are ignored when reusing a disk.

Setup checks the remembered runtime folder for an existing Android disk. If one
is found, **Use current Android installation** is checked and shows its full path
and configured storage size. Reuse preserves the disk, installed games and saves;
grow storage later in Settings. If its size cannot be read from `config.ini`,
restore that configuration before reusing the disk.

Uncheck the box for a new installation, then choose a folder and storage size.
Setup creates an `AXRB Runtime` subfolder there and checks only that destination;
an old disk elsewhere does not constrain the new disk size. An occupied destination
is rejected rather than overwritten. The old installation is left untouched, and
its games and saves are not copied. The launcher library and login stay in their
existing profile; choosing a new runtime folder does not reset them.

The saved runtime changes only after setup succeeds. Failed or cancelled setup
keeps the previous runtime selected after a launcher restart. Successful setup
refreshes installed-game flags from the selected Android disk without removing
library entries, ownership information or downloaded files.

### Portable ZIP

**Strict portability is not yet achieved.** The pinned Windows ADB was observed
loading `%USERPROFILE%\.android\adbkey` despite portable Android-home and temp
settings. A child `USERPROFILE` override also did not redirect the Windows
profile API. The containment below does not eliminate this machine-local
credential dependency. Choosing a maintained patched ADB build or another
verified compatibility strategy requires a maintainer decision; neither is
implemented here. Full emulator filesystem tracing and a moved-AVD cold boot
with the original folder unavailable are also not yet verified.

Extract `AXRB-Portable-<version>.zip` into a writable folder and run `AXRB.exe`.
Keep the `AXRB.portable` marker beside it. The portable folder contains:

- `data/`: launcher profile, Chromium storage/cache, logs and crash dumps.
- `temp/`: temporary files, also used by AXRB's child processes. Windows never
  prunes this folder, so AXRB removes entries older than a week at startup.
- `downloads/`: default game downloads, copied imports and patched APKs.
- `AXRB Runtime/`: default managed SDK, Android configuration, AVD and saves.

The marker takes precedence over `--user-data-dir`. Setup, downloads, patch
output and saved diagnostic reports must remain inside the portable folder.
External APKs and content files are copied into it; the originals are untouched.
External setup archives can be read without becoming ongoing dependencies.
Put the complete optional ovrport CLI distribution inside the portable folder
before selecting it. Java, if that CLI requires it, remains a machine prerequisite.
SteamVR-launched packaged helpers restore the same portable paths and temporary
environment without requiring the launcher to remain open.

The direct-launch EXE has no marker and keeps its Windows user profile, external
runtime defaults and reference-in-place imports.

Close AXRB and Android before moving or copying the entire portable folder.
Internal paths follow the new location even if the original copy still exists.
Setup repairs moved AVD paths and discards its old quick-boot snapshot while
preserving Android storage, installed games and saves. A runtime-app update or
path repair does not redownload SDK components whose pinned receipts remain valid.

Portable mode stores the Meta session **unencrypted** in `data/meta-session.txt`
inside the portable folder so it can move between computers. Anyone with a copy
can use that session for the store and owned-content downloads until it expires.
Sign out before sharing or discarding the folder. Signing out deletes both local
session formats but does not revoke copies already taken; revoke the session in
Meta account settings if the folder or drive is lost. Nonportable mode keeps using
Electron safeStorage (Windows DPAPI).

This is application-level portability, not an OS sandbox. Windows, GPU drivers,
Java and an independently installed SteamVR can maintain their own system files.
AXRB constrains the output paths it passes to an optional ovrport CLI; it cannot
sandbox arbitrary third-party code. SteamVR's external registration must be
refreshed by launching the game through AXRB after moving the folder.

## Live diagnostics

Open **Debug** at the bottom of the launcher during setup or gameplay. Resize
the panel by dragging its top edge, or focus the separator and use the arrow
keys. Search messages, tags or process IDs, and filter by source or severity.
**Pause** freezes the display, not capture. **Copy visible** copies matching
lines; **Save report** writes the full diagnostics bundle to a local text file.

Android capture discovers the configured AVD through the isolated ADB server on
port 5038; it does not assume a fixed AVD name or device serial. Emulator, host
and launcher output remains available when Android is offline. The timeline
uses host receipt order and preserves Android timestamps separately.

Game-launch output is drained for up to 500 ms before reporting exit, so
inherited pipe handles do not hide final messages or stall completion.
Other active diagnostic streams are not flushed.

Capture continues with the panel closed and retains recent history after a
game exits or the launcher restarts. Retention is bounded to 5,000 entries and
approximately 2 MiB, stored in `diagnostics/recent.json` under the launcher
profile (`%APPDATA%\AXRB` by default). Older entries are discarded, not archived.
Redaction is best effort: known credentials (including Basic and Bearer auth) and
Windows identity are masked. Streams are kept separate, and long or unfinished
messages retain their non-secret context instead of being omitted wholesale.
Tail reads recover line prefixes within one extra read window; exceptionally
long or fragmented credentials can still escape recognition. Review reports
before sharing. Nothing is uploaded automatically. The Settings upload action
requires a preview and publishes that reviewed report.

## Build Windows packages

From a configured Windows development checkout, run
`powershell -ExecutionPolicy Bypass -File launcher/build.ps1`.
The direct-launch EXE and matching source archive are written to `out/releases`.
The script keeps the version from `launcher/package.json`, runs the launcher tests,
and writes `SHA256SUMS-<version>.txt`. Use `-SkipNative` when only launcher files
changed and the existing native/runtime artifacts are still current; use
`-SkipTests` only for a packaging retry after tests have already passed.
Add `-Portable` to build `AXRB-Portable-<version>.zip` instead of the EXE, with
`SHA256SUMS-<version>-portable.txt`. `npm run dist:portable --prefix launcher`
uses the same build wrapper. Neither package includes an existing profile or
managed Android installation.
Each build finalizes the shared source ZIP, then refreshes checksum manifests for
both package modes present in `out/releases`. Build both packages from the same
source checkout before publishing; refreshing a manifest does not rebuild its package.
Distribute the matching source archive alongside either launcher package.
Code signing uses electron-builder's standard certificate environment variables;
without a signing certificate, the EXE is unsigned.

Release preparation downloads Khronos's official `OpenXR.Loader.1.1.60.nupkg`
and verifies its pinned SHA-256 before extracting only the Windows x64 desktop
loader. `openxr_loader.dll` is bundled beside `axrb-host-bridge.exe` and recorded
in `distribution.json`. The loader uses the system's active OpenXR runtime;
SteamVR's private loader is neither required nor bundled.

The host tries `AXRB_OPENXR_LOADER` first (a developer/debug override), then an
absolute path to the DLL beside the host, then ordinary Windows DLL lookup.
Windows native builds copy the pinned Khronos DLL beside the development host;
a failed override falls through to these defaults.
Paths support Unicode, and diagnostics report each attempted loader location
and its Windows error code. If no loader is available, startup stops. A bundled
loader does not replace the requirement to install and configure an OpenXR
runtime for the headset.

## Develop

Requires Windows x64, Visual Studio 2022 Build Tools with **Desktop development
with C++** and a Windows SDK, CMake 3.22+, Node.js 24+, Python, and JDK 17+.
The installed launcher bundles AXRB's native files; a source checkout must build
them before first-run Android setup can boot the emulator.

From the project root, install the development SDK and build the native files:

```powershell
# Read https://developer.android.com/studio/terms before accepting.
powershell -ExecutionPolicy Bypass -File scripts/build/android_sdk.ps1 -AcceptLicense
powershell -ExecutionPolicy Bypass -File scripts/build/windows.ps1
powershell -ExecutionPolicy Bypass -File runtime/apk/build_apk.ps1 -Abi arm64-v8a
python scripts/build/runtime_adapters.py
```

SDK setup installs the pinned NDK, API 29, build-tools and Android CMake under
`%LOCALAPPDATA%\Android\Sdk`. It uses `JAVA_HOME` or `javac.exe` on `PATH`; `-Jdk`
can select another JDK. It requires explicit license consent and does not install
the emulator or replace the launcher's separate managed Android SDK.
At launch, development adapters use `ANDROID_NDK_HOME` when set; otherwise they
look in the selected SDK, `ANDROID_HOME`, `ANDROID_SDK_ROOT`, then the standard
development SDK above. The managed emulator SDK does not need its own NDK.
An invalid explicit `ANDROID_NDK_HOME` is reported rather than silently ignored.
Packaged installations continue to use their checksum-verified bundled adapters.
Native outputs go under `out/`; keep these generated files out of commits.

For a Windows GPU-layer-only rebuild:

```powershell
powershell -ExecutionPolicy Bypass -File host/gpu/build.ps1
ctest --test-dir out/gpu -C Release --output-on-failure
```

The GPU build selects Visual Studio explicitly, even if CMake defaults to Ninja.
It normally uses Vulkan headers from the development NDK. `-VulkanHeaders`
accepts a headers directory containing `vulkan/` and `vk_video/` when using a
separate Khronos headers checkout; `-Generator` selects another Visual Studio
version.

After the native build, start the launcher:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run/run_launcher.ps1
```

The first launch installs the locked npm dependencies and downloads Electron.
You can also double-click **AXRB Launcher.cmd** in the project root.
For development: `cd launcher; npm ci; npm start`.
  To bypass GPU, memory, and architecture checks while debugging setup, launch the
  development or packaged app with `--axrb-debug` (the hypervisor check remains
  active). `AXRB_DEBUG=1` is also accepted for scripted launches.

The renderer uses React, Tailwind CSS and local shadcn/ui components. Vite builds
it into `dist/`; both start commands rebuild before opening Electron. No local
web server is needed. `npm run build` builds the renderer without starting it.
Source components are in `ui/`, with shared shadcn components in `ui/components/ui`.

## Use

- **Library:** Refresh imports launchable apps from the already-running selected
  AVD. It does not start the emulator just to scan. Cached games stay visible when
  Android is stopped. Install/play starts Android as needed.
- **Store:** Search the live Meta catalog; open a listing and add it to the
  library. Purchases open on Meta's site. Sign in to list your Quest entitlements
  and access downloads; a Rift purchase is not a Quest entitlement.
- **Connect Meta:** Sign in on Meta's hosted page. Credentials are never sent to
  an AXRB service. Nonportable mode encrypts the account token with Electron
  safeStorage (Windows DPAPI); portable mode stores it unencrypted inside its
  `data/` folder. Tokens are never sent to the launcher renderer or written in logs.
- **Downloads:** Choose a Quest build. APK, OBB and binary asset files are saved
  with original filenames. Transfers can be cancelled/retried, incomplete files
  stay `.part`, resume requires an ETag, and completed files receive a local
  SHA-256 for verification before installation. The download folder is selectable.
- **Game actions (⋯):** Versions, add-ons, patching, content imports and installation
  updates are in the game's menu. Runtime settings are collapsed under Settings.
- **Android permissions:** With Android running, open an installed game's menu
  and choose **Android permissions**. These controls manage runtime permissions
  for Android's main user (user 0), not install-time permissions or other users.
  **Allow** grants a permission; **Allowed** revokes it. **Allow all** applies
  grants in sequence and keeps successful changes if Android refuses a later one.
  Grant state is re-read from Android; a successful command alone is not treated
  as success. A pending-dialog notice is device-wide and may belong to another
  app. Relaunch the requesting app after changing its permissions.
- **Add-ons:** Ownership must be returned by Meta before a separate DLC download
  is enabled. Some DLC is only an entitlement to content inside the base game,
  with no downloadable file. Entitlement and asset-discovery compatibility inside
  a patched game still depends on ovrport; copying files alone cannot guarantee it.
- **Local games:** Import an APK (including already-patched APKs) and optional
  expansion files. Originals are referenced in place, not deleted or modified.
  Import recognizes standard launcher entries and enabled, exported `MAIN`
  activities or aliases marked `INFO` or Oculus `VR`, even without a phone launcher entry.
- **Patch:** Configure the separately installed ovrport **CLI** `.exe` or `.jar`
  in Settings (the JAR requires Java), then save Settings. Choose **Patch with
  ovrport** from the game's menu. A verified compatibility profile applies its
  preset automatically; other games and unmatched versions show the configured
  CLI's available patches, with its recommended selection checked. Review the
  options, optionally supply comma-separated advanced arguments, then choose
  **Apply selected patches**. Cancel leaves the APK untouched.
  Use a current CLI with `patches --json` support; profiled patches also require
  `--extra-patches` support. Older CLIs fail visibly instead of silently ignoring
  a preset. AXRB bundles neither ovrport nor Java.
  Patching writes a separate `axrb-patched/<input-name>-axrb.apk`; installation
  remains a separate explicit action. For an already-installed game, choose
  **Update installation** afterward. General patching does not guarantee every
  game's compatibility with AXRB.
- **Install:** Uses `adb install -r`, preserving app data. Signature conflicts
  report an error; the launcher does not uninstall the existing app. Assets are
  copied into their package-specific `Android/obb` or `Android/data` destinations.
  With root ADB, copied files and their parent directories up to the package
  directory are assigned to the installed app; unrelated files, storage groups
  and permission modes are left unchanged. After downloading more content,
  use **Update installation** to copy it into Android.
  Installation shows a byte-weighted percentage, copied/total size and the current
  filename in Downloads, game details and ZIP install notifications. During asset
  transfers, copied bytes are sampled roughly once a second; existing files do
  not count until replacement or new writes are observed. If progress queries
  are unavailable, installation continues with updates as files finish.
  APK installation and the final **Finishing installation…** flush remain
  indeterminate; completion is reported only after the flush succeeds.
- **Play:** Calls `scripts/run/run_windows_game.ps1`, preserving automatic OpenXR eye
  resolution, SteamVR name/icon, GPU texture sharing, and the save-aware shutdown.
  Closing the game preview stops the game. Closing the launcher does not
  intentionally stop a running game; stop it with its preview window.

Managed installations use `axrb-managed-api36` on port 5584 with four vCPUs and
8 GB guest RAM. With the direct-launch EXE, Android, downloads and logs live outside the
application folder and survive launcher updates/uninstallation. Portable mode
defaults to the adjacent folders described above; retain those folders when
updating the launcher. Development checkouts can continue using an existing SDK
and AVD. Windows features and SteamVR remain user-installed.

### Verified game profiles

`core/game-compatibility.json` is the bundled, versioned database of maintained
patch and launch actions. The initial profile is **The Climb 2 2.2**
(`com.crytek.climb2`): recommended ovrport patches plus the experimental
`patch_vrapi_openxr` adapter, and projection-layer precomposition at launch.
Use an experimental CLI build containing the adapter. The profile does not
establish complete gameplay compatibility.

**Batman: Arkham Shadow 1.4.1** (`com.camouflaj.manta`, versionCode 350961) and
**North Star 1.0.1** (`com.meta.samples.NorthStar`, versionCode 101) use
ovrport's recommended patches alone, recorded as `"ovrport": { "recommended":
true }`. Both are Unity titles whose Oculus XR plugin refuses to start VR off
Quest hardware until patched. Batman also carries the Meta Platform SDK loader
and Meta XR Audio for Wwise, which the recommended set replaces and patches. A
matched profile patches without showing the picker, and its summary tells the
player to patch before installing.

Game details disclose the local APK's matching profile. **Patch** re-inspects
the actual input file before selecting a preset or showing manual choices, and
again when applying those choices. Replacing an APK therefore refreshes its
saved version rather than reusing a stale preset. A different package is rejected
without changing the library record. If the input gains a verified profile while
the picker is open, reopen the details instead of applying stale manual choices.
A known package with a missing or different version gets no automatic patch
actions; unknown packages also use manual choices.

**Play** checks the selected Android runtime's installed package, exact version
and launch activity on every launch, independently of the downloaded APK.
Downloading or patching another version does not change the installed build's
profile. A failed installed-identity check stops launch instead of falling back
to library metadata. Notifications identify an applied profile or warn when
the installed version is unverified. Global runtime settings still apply.

Profiles match exact package names and optional exact-string `versionName`
(the library's `version` field) and/or `versionCode` selectors. Every supplied
field in a rule must match; any rule may match. Omit `versions` only for a
verified package-wide action. Keep large version codes as JSON strings.
Unknown schema/action keys and ambiguous matches are errors, not silent
fallbacks. Derived compatibility data is never written to `library.json`.
After a base APK download, both version fields are taken from its inspected
manifest rather than the store's version label or a previous library record.

### Projection-layer compatibility

**Settings → Precompose projection layers** is **off by default**. The Climb 2
2.2 profile enables it automatically for that game version. The global switch
can force it on for other games with incorrect multi-projection rendering.
Stop the game, change the switch, click **Save**, then launch again. Profiles
never turn off an enabled global setting; turning the global switch off does
not disable a matched profile's requirement.

- **Off, without a matching profile:** submit layers natively when they fit the OpenXR runtime's capacity.
- **On:** combine stacks containing two or more projection layers into one stereo
  projection, preserving their contents, ordering and alpha. Single-projection
  and within-capacity native quad/equirect paths remain unchanged.
- **Either setting:** composition required by the runtime's layer-capacity limit
  remains automatic; the host still uses the runtime's reported limit.

Precomposition adds GPU work and replaces independent runtime handling of those
layers with the runtime's display-time camera and optical FOV for each eye.
Wider or rotated source projections are sampled across that visible target,
rather than clipped to the first layer's frustum. It is a compatibility
workaround, not a performance option; its cost depends on the game and runtime.
It does not change VrApi controller poses, device identity or tracking origin,
and changing the setting does not require repatching or reinstalling the APK.
The setting requires the updated AXRB launcher, launch script and host together.

For script launches, add `-PrecomposeProjectionLayers` to
`scripts/run/run_windows_game.ps1`; omitting it explicitly disables the workaround
for that host even if the parent environment enabled it. Direct host launches
can set `AXRB_PRECOMPOSE_PROJECTION_LAYERS=1` before startup. Only the exact value
`1` enables the policy; unset or `0` leaves it off. The policy is selected at
host startup, not changed during an active session.

## Data and limitations

The direct-launch EXE uses `%APPDATA%/AXRB/library.json` for games, settings and task
history; portable mode uses `data/library.json` beside the executable.
`meta-session.bin` in the nonportable profile stores the encrypted Meta token;
portable mode uses plaintext `data/meta-session.txt` inside the portable folder.
Both session filenames are ignored by Git at any depth and excluded from source
archives. `/AXRB-Portable/` is also ignored for local portable installations.
Downloads default to `~/Downloads/AXRB/<app-id>/<build-id>/` with the EXE
or `downloads/<app-id>/<build-id>/` in portable mode. Games' saves stay in the AVD.
Five GB of free disk headroom is reserved before downloads to keep Android
bootable. Meta APIs used by community launchers are undocumented and may change;
API errors are shown rather than treating missing content as successful installs.
Library pagination is reported if Meta returns a partial entitlement response;
additional owned apps can be added from the store.

## Verification

```powershell
npm test --prefix launcher
npm run smoke --prefix launcher
```

Unit tests cover Quest filtering, SSO challenge validation, DLC entitlement
selection, APK/OBB plans, download integrity/resume, unsafe paths/redirects,
atomic library persistence, exact-version profiles, CLI capability/selection
validation and PowerShell launch switches. The desktop smoke test uses a
separate `out/launcher/smoke` profile, navigates Library/Settings/Downloads,
uses a local store listing fixture, and checks filters, dialog/menu keyboard
focus, progress display and minimum window width. It also checks profile
disclosures, Patch/Play notifications, manual patch options and cancellation
through production IPC/React with process and APK boundaries stubbed.
Screenshots are written to the smoke profile. No Meta account, live store
search, APK download, installation, patching or Android game launch is needed.

Meta login challenge creation, public storefront search and installed-game scan
have been checked live. Account-specific downloads/install/DLC still need a
signed-in account test; fixture coverage is not an end-to-end Meta download test.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for RiftLift attribution.
For educational-use and lawful-content requirements, see
[../DISCLAIMER.md](../DISCLAIMER.md).
