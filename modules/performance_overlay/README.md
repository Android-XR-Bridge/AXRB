# Optional performance overlay

Build with `-DAXRB_ENABLE_PERFORMANCE_OVERLAY=ON`; default is OFF.
With OFF, the stock basic FPS HUD remains and this module is not compiled
or linked. The entire module directory may be omitted from an OFF build.

Enable the launcher's existing `-FpsHud` event. The module adds a centered
headset quad and top-middle mirror panel; F1 in the mirror toggles it.
It counts unique nonempty gameplay image sequences on successful OpenXR
submissions, not sequence gaps, repeated images or compositor ticks.
It does not measure physical panel scanout or prove every submitted frame
was displayed by SteamVR.

Optional environment variables:

- `AXRB_PERFORMANCE_FRAME_LOG`: new CSV path for received/submitted events.
  Background writer, bounded queue, explicit lost-event count, 256 MiB cap
  and 60 GiB free-space reserve. Existing evidence is not intentionally replaced.
- `AXRB_PERFORMANCE_CPU_FILE`: path to the collector's atomic 3-line snapshot.
  Samples older than six seconds are unavailable, never reported as zero load.

The optional Windows Python collector is independent of frame rendering:

```text
python collect_guest_cpu.py --adb <adb.exe> --serial <emulator-PORT> --adb-port 5037 --package <package> --parent <launcher-PID> --output <snapshot.txt> --history <new-history.jsonl>
```

It exits with the parent, samples Android /proc every two seconds, and optionally
logs bounded host/resource history (128 MiB). NVIDIA data is best-effort.
CPU thread percentages are shares of one guest core, not host-total CPU.
No collector runs automatically just because the module is compiled.

All display, bitmap, journal and collector implementation is in this directory.
Core code contains only conditional telemetry hooks; default builds do not run
the collector or journal. Tests are gated by the same build option.
