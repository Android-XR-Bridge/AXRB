# Performance scan

A frame rate complaint cannot be answered from the logs. The logs record what
happened; a slow session is a question about what was *busy*, and nothing in
`host.err` or logcat says whether the cost sat in the guest, on the GPU, or in
the Windows compositor. `scripts/run/performance_scan.ps1` samples all of them
over one window so the numbers can be compared against each other.

Everything it does is read-only. It pushes nothing to the guest, changes no
settings, and de-identifies the report before printing it, so the output is
safe to hand to someone else for help.

## Running it

From the launcher: start a game, open **Settings**, and use **Performance
scan**. The control only appears while a session is running, because that is
the only time there is anything to sample. *Run scan* shows the report; *Run &
upload* publishes it to the same paste service as the diagnostics bundle and
copies the link.

By hand, against a release install (the scripts live under the installed
`resources\runtime` directory):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run\performance_scan.ps1 `
    -OutFile "$env:USERPROFILE\Desktop\axrb-scan.txt"
```

Start the game and put the headset on first. Useful parameters:

| Parameter | Default | Notes |
| --- | --- | --- |
| `-Seconds` | 10 | Sample window. Longer averages out a stutter; shorter is easier to hold still for. |
| `-Package` | foreground app | Name the game if the scan picks the wrong process. |
| `-Port` | 5580 | The emulator's ADB port, matching the launcher's Android settings. |
| `-Sdk` | `%LOCALAPPDATA%\Android\Sdk` | Only needed for a non-default SDK location. |
| `-OutFile` | — | Write the report to a file instead of standard output. |

The whole run takes the window plus about five seconds of setup.

## What the sections mean

**Inventory** — CPU, GPU and memory, the registered OpenXR runtime, and
SteamVR's render overrides. `supersampleScale` is an *area* multiplier: 1.5
means 1.22× more pixels along each edge, and every one of those pixels is
rendered inside the guest. A manual value here outweighs anything else in the
report.

**Windows processes** — CPU as a share of one core, so a number above 100% is
a process using more than one. `qemu-system-x86_64-headless` is the whole
Android guest seen from outside; `axrb-host-bridge` is AXRB's own compositing;
`vrserver` and `vrcompositor` are SteamVR.

**GPU engines** — reported per engine rather than as one figure, because Task
Manager's single percentage is the busiest engine and a copy-bound title looks
nothing like a shader-bound one. `nvidia-smi`, when present, adds the power
draw and clocks that tell a loaded card from an idling one.

**Android guest** — per-thread CPU inside the game, split into user and system
time. This split is the one that matters: user time is translated ARM code, and
system time is syscalls, which for a Vulkan title is the graphics pipe out of
the guest. The emulator has no PMU, so `simpleperf` cannot sample here and this
split is the available discriminator. Involuntary context switches per second
separate a thread that is genuinely busy from one being preempted by the host.

**Bridge timings** — the five-second summaries both sides already publish, with
only the most recent window of each counter kept. `host-image-arrival` is the
wall-clock spacing between frames arriving from the guest, so its rate is
effectively the game's frame rate. The host section is marked stale when
`host.err` has stopped being written, which means it describes an earlier
session.

**Observations** — only the conclusions the numbers above can settle on their
own. An empty list is not a clean bill of health; it means nothing crossed a
threshold that could be checked without a human.

## The shape of the usual answer

A session limited by one guest thread at nearly 100% of a core, while the other
vCPUs idle and the GPU sits in the low tens of percent, is the common case. It
is not a transport problem and not a resolution problem: it is single-threaded
translated ARM code, and neither more vCPUs nor a smaller render target moves
it much. See `docs/performance_backlog.md` for what has been measured there.
