# Experimental import

Imported for local evaluation from the user-supplied `quest-bridge-2026-09-25.zip`.
SHA-256: `8568f488b1841292c209c9c3f8ef659c6c2576edbc08d0dbfac8947d7814f2ab`.

The archive's `game/` and `helper/` directories were excluded, including games,
APKs and prebuilt helper executables. Its OpenXR SDK headers, loader and Apache
license are retained under `third_party/openxr`. No top-level license or author
declaration was supplied for Quest Bridge itself. This import does not assign
AXRB's MIT license to that code; resolve its redistribution terms with the sender
before shipping it. AXRB's normal release packaging has not been extended to
ship this experimental runtime.

The original README describes an earlier demo path. AXRB builds `qb-test`, the
more complete native application runner, which forwards Vulkan/OpenXR directly
to the desktop drivers. `qb-host`, `qb_runtime`, `qb-app` and `qb-guest` remain
upstream demos, not the launcher integration.

AXRB changes: configurable NDK, clean build fixes, OpenXR loader deployment,
native probe registration, a lifecycle probe, UTF-8 process manifest, full-length
entry paths, package-neutral default, active-runtime selection, lifecycle Stop,
application-requested exit, Unreal activity-name discovery, and reduced Unity
per-frame logging. Launcher preparation and tests live outside this import.

See [test guide](../../docs/quest_bridge_experiment.md).
