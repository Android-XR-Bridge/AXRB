# Offline backup

`scripts/backup/offline_backup.ps1` makes a backup you can build from, run and
change without GitHub or any of the project's download hosts. Run it on the
Windows PC you build AXRB on, from an up-to-date checkout:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/backup/offline_backup.ps1 `
    -Destination D:\AXRB-Backup -AcceptAndroidLicense -IncludeToolchains `
    -GamesDir "D:\AXRB Downloads"
```

Rerun it whenever you want to refresh the backup. Git mirrors are updated in
place, and files whose checksum already matches are not downloaded again. The
script ends with a list of anything it could not save and exits with code 1 if
the list isn't empty.

| Option | Effect |
| --- | --- |
| `-Destination` | Backup folder (default `%USERPROFILE%\AXRB-Offline-Backup`). |
| `-AcceptAndroidLicense` | Installs the Android development SDK with `sdkmanager` before copying it. Read the [Android SDK terms](https://developer.android.com/studio/terms) first. |
| `-IncludeToolchains` | Also saves installers for Node.js, Python, JDK 17, Git and CMake, plus an offline Visual Studio 2022 Build Tools layout (several GB). |
| `-GamesDir` | Copies your downloaded game files (APK/OBB) into `games\`. |
| `-SkipBuild`, `-SkipReleases`, `-SkipAndroidSdk` | Leave those parts out. |

Plan for roughly 10 GB without toolchains or games, and 20 GB or more with them.
Keep a second copy on another drive or in cloud storage.

## What the backup contains

| Folder | Contents |
| --- | --- |
| `git\AXRB-upstream.git` | Mirror of `Android-XR-Bridge/AXRB`: every branch, tag and pull request. |
| `git\AXRB-fork.git` | Mirror of your fork (the checkout's `origin`). |
| `git\deps\` | Mirrors of OpenXR-SDK, MinHook, Dynarmic and OpenXR-SDK-Source, which the build clones. |
| `bundles\` | The two AXRB mirrors as single-file `git bundle`s, plus `local-checkout.bundle` with your checkout's branches, including commits you never pushed. |
| `source\working-copy.zip` | Your checkout as it is now, including uncommitted and untracked files. |
| `keys\` | The runtime APK signing key (`.local/keys/runtime.keystore`). Keep it: rebuilt runtime APKs then still install over existing ones. |
| `downloads\android-components\` | Emulator, platform tools, build tools and Android 16 system image used by first-run setup. |
| `downloads\build\` | Pinned build downloads: Android command-line tools, the OpenXR loader, embedded Python, ISPC and Boost. |
| `android-sdk\` | The Android development SDK: NDK 27.3, CMake 3.22.1, platform 29, build tools 36.1.0, platform tools. |
| `caches\` | npm, Electron and electron-builder caches captured during the build. |
| `releases\` | Every published GitHub release (installers, notes and checksums) for upstream and your fork. |
| `builds\<version>\` | The portable launcher and source archive built from your checkout. |
| `toolchains\`, `games\` | Optional, see above. |
| `SHA256SUMS.txt` | Checksums of the backup's files (except git, caches, SDK and the Visual Studio layout). |

Meta and ChatGPT sign-ins are intentionally not backed up.

## Using the backup

**To just play,** unzip `builds\<version>\AXRB-Portable-<version>.zip` or run an
installer from `releases\`. During first-run setup, choose the four archives in
`downloads\android-components` instead of downloading them. The launcher checks
them against the same pinned checksums.

**To build and change the code offline:**

1. On a new PC, install the toolchains from `toolchains\`: Visual Studio 2022
   Build Tools from `vs2022-buildtools\vs_setup.exe`, Node.js, Python, JDK 17,
   Git and CMake.
2. Restore a checkout and build it:

   ```powershell
   powershell -ExecutionPolicy Bypass -File D:\AXRB-Backup\offline_restore.ps1 -Checkout C:\AXRB -Build
   ```

   This clones your fork's history (`-From upstream` uses upstream's), adds the
   other mirror as a remote, restores the signing key, places the pinned
   downloads where the build looks for them, restores the Android SDK into
   `%LOCALAPPDATA%\Android\Sdk`, and points git, npm and Electron at the backup.
3. For later builds in a new PowerShell window, dot-source the script first to
   set the offline environment again:

   ```powershell
   . D:\AXRB-Backup\offline_restore.ps1 -Checkout C:\AXRB -EnvOnly
   .\scripts\build\build-launcher.ps1 -Portable -SkipTests
   ```

Commit your changes in the restored checkout as usual, then rerun the backup
script from that checkout so `local-checkout.bundle` and `working-copy.zip`
pick them up. Don't push your work into the `git\*.git` mirrors: the next
backup run makes them match GitHub again and removes branches that only exist
there.

Test a restore once while you still have internet. Run it with the network
disconnected and confirm the build finishes: that's the only real proof that
nothing is missing.
