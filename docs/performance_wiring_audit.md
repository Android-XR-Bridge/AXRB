# Launcher performance wiring audit (2026-09-30)

Checked the generic changes described in `cpu_pressure.md`, the launcher call
paths, and the 1.0.0 packaged resources. This is a wiring audit, not a repeat
of the reported CPU measurements or a headset gameplay validation.

| Change | Launcher path and activation |
| --- | --- |
| Cached buffer memory | `run_windows_game.ps1` invokes `android_runtime_policy.py`, which installs and enables the checksum-verified x86-64 Vulkan layer for the launched package. The layer filters buffer memory requirements by default; `debug.axrb.cached_buffer_memory=0` disables it. |
| Native Unity skinning | The same layer calls `install_guest_accel_once()` when creating a Vulkan instance. Requires a supported translator, host FMA, and matching Unity kernel bytes. `debug.axrb.guest_accel=0` disables it. No North Star ocean replacement is called. |
| Hypervisor APIC | Emulator startup resolves `LocalApic=Auto` to `Hypervisor` only with `GuestClock=TscCorrected`, then sets `AXRB_WHPX_HV_APIC` for the clock helper. The packaged helper contains the APIC implementation. |
| ASG graphics transport | Emulator startup defaults `GraphicsTransport` to `Asg` and writes `hw.gltransport=asg` before boot. The inspected managed AVD already contains this setting. A running emulator needs a restart to adopt a changed transport. |

New launcher profiles choose corrected TSC when the helper is present. Existing
saved settings override that default. The inspected local profile explicitly
has `guestClock=Default`, so automatic hypervisor APIC activation is disabled for
that profile. The audit does not overwrite an existing clock preference.

Fixed: `Runtime.launch()` now forwards the saved clock and optional APIC setting
to the game script, matching `Runtime.startEmulator()`. Previously the fallback
boot inside that script could ignore the saved choice. An integration test
executes fixture PowerShell scripts through both paths for corrected-TSC /
hypervisor and default-clock / QEMU selections.

Verified the packaged clock DLL, Vulkan layer and policy scripts against the
distribution manifest, and checked the expected APIC/cached-buffer/skinning
markers in the binaries. The cached-buffer policy regression test passes with
Vulkan headers explicitly supplied to the native test build. That test was
previously omitted by CMake when `find_path` could not locate Vulkan headers.

The configured emulator was not reachable during the audit; no game was
started and no active-session optimization claim is made. The report's 48% and
25% results remain the earlier agent's measurements under its stated conditions.
Optional frame-wait experiments, diagnostic HUD builds, x2APIC and alternative
translators are not prerequisites for the four generic changes above.

## Follow-up: automatic clock selection

At the user's request, launcher startup now migrates legacy `Default` clock
settings to `Auto` once (`clockPolicyVersion=1`). Future explicit overrides are
preserved. Automatic emulator preparation checks both helper files and the
supported QEMU checksum before selecting `TscCorrected`, matching the game
script's existing automatic selection. Unsupported or incomplete installations
retain the default clock. The inspected local profile was updated to `Auto`,
and selection against its installed emulator returned `TscCorrected`. This
takes effect on the next emulator boot; an already-running emulator is not
reconfigured by changing the setting.
