# CPU pressure: where it went and what removed it

Investigation of 2026-09-28 on the Ryzen 5 9600X / RTX 5070 Ti machine,
Android 16 image, emulator 36.5.11 (WHPX), 4 vCPUs, stock Google translator
(`libndk_translation.so`) unless stated. Headset not worn: the host session
stays SYNCHRONIZED, so the scenes are menu/start scenes, not gameplay.

## Summary

Everything below applies to every game by default; nothing is selected per
game. Interleaved matrix, synthetic head pose, medians of six 20 s windows
(`matrix-final-*`):

| Game | Configuration | Host cores | Host ms/frame | FPS |
| --- | --- | --- | --- | --- |
| Nexus | baseline | 1.84 | 21.3 | 86.7 |
| Nexus | cached buffers + native skinning | 1.25 | 14.0 (-34%) | 89.6 |
| Nexus | + hypervisor APIC + asg (default now) | 0.99 | 11.0 (-48%) | 89.8 |
| North Star | baseline | 2.11 | 25.3 | 83.6 |
| North Star | cached buffers + native skinning | 1.82 | 20.5 (-19%) | 88.8 |
| North Star | + hypervisor APIC + asg (default now) | 1.61 | 18.9 (-25%) | 85.1 |

The baseline is the state before this work: uncached host-visible buffers,
Unity's own skinning code, QEMU's local APIC and the pipe graphics transport.
North Star's remaining cost is its own and the engine's translated code
(Finding 6).

## Test environment

`experimental/cpu/` contains a repeatable harness:

| Script | Purpose |
| --- | --- |
| `session.py` | Boots the isolated `axrb-digitalis-test` AVD fresh, launches a game, warms up, then measures host QEMU CPU, guest `/proc` CPU per thread, guest interrupt rates and the frame rate over fixed windows. Options for translator properties, AVD config, simpleperf, WHPX exit profile, frame capture and a synthetic head pose. |
| `matrix.py` | Runs configurations in interleaved ABBA order and prints medians. |
| `switch_translator.py` | Switches the test AVD between the stock translator and Digitalis. |
| `jit_breakdown.py` | Attributes translated-code samples per thread to guest libraries (Digitalis region map). |
| `vk_memory_probe.cpp` | Times CPU writes/reads into each host-visible Vulkan memory type. |
| `kill_run.py` | Stops a stuck harness run (test AVD only). |

The main figure is **host CPU per unique frame** (QEMU process CPU divided by
the frame rate). Profiling runs (`--simpleperf`) are not comparable with
unprofiled runs; simpleperf itself costs roughly 0.3 cores.

## Finding 1: skinned vertices were written to uncached memory

Profiles showed one libunity function taking 94% of all translated-code
samples on Digitalis (about two thirds of the game's CPU) and about 30% of the
process on the stock translator. It is Unity's NEON CPU skinning kernel
(`BoneInfluence4` blend, position + normal + tangent). Unity writes the skinned
vertices directly into a Vulkan host-visible buffer.

`vk_memory_probe` on this guest (ns per 40-byte vertex, skinning store pattern):

| Memory type | Flags | Write | Read |
| --- | --- | --- | --- |
| plain RAM | | 1.3 | 1.0 |
| 2 | HOST_VISIBLE, HOST_COHERENT | **470** | 439 |
| 3 | HOST_VISIBLE, HOST_COHERENT, HOST_CACHED | 2.4 | 6.0 |
| 4 | DEVICE_LOCAL, HOST_VISIBLE, HOST_COHERENT | 29 | **3130** |

The guest maps types 2 and 4 uncached / write-combined. The skinning loop ran
at about 230 ns per vertex whether translated or native: the stores, not the
arithmetic, were the cost. The Vulkan layer's cached-buffer policy
(`runtime/vulkan/cached_buffer_policy.h`) removes the uncached host-visible
types from a buffer's `memoryTypeBits` only when that buffer can also use a
driver-reported HOST_CACHED | HOST_COHERENT type. That is a legal Vulkan choice
and x86 keeps cached mappings coherent with device access, so no flushes are
added. Images are never affected.

## Finding 2: native replacements for Unity's skinning kernels

`runtime/vulkan/guest_accel/` replaces Unity's nine CPU skinning kernels
(1/2/4 bones x position / +normal / +tangent) with native x86-64 code. Both
translators export `MakeTrampolineCallable`, which makes a guest address run a
host function; the x86-64 Vulkan layer already runs inside the game process.
Kernels are matched word for word (the same bytes occur in the Unity 2021.3
and the newer Unity build in `out/unity`), and the native code repeats the
guest's multiply / fused-multiply-add order and store widths, so the results
are bit-identical: `tests/native/unity_skinning_equivalence.cpp` runs Unity's
original machine code (arm64 build, executed by the translator) and the
replacement on 360 randomized cases, including subnormals and signed zeros,
and compares the outputs byte for byte. `debug.axrb.guest_accel=0` disables it.

With cached memory in place the native kernels save a further few percent of a
core on the stock translator; on Digitalis, whose translation of this loop is
several times slower, they matter more.

## Finding 3: what the graphics transport and vCPU count change

`hw.gltransport=asg` alone saved about 5%; combined with cached memory it was
within noise of `pipe`. The remaining overhead is virtualization: the host
QEMU process uses about 0.45 cores more than the guest reports busy.

An opt-in WHPX exit profile (`AXRB_WHPX_EXIT_STATS=<file>` with the clock
helper, `host/clock/exit_stats.h`) shows about 21,000 local-APIC MMIO exits
per second at ~6.6 us of QEMU userspace time each (this emulator's WHPX has no
in-hypervisor APIC), 4,600 HLT exits and 5,400 vCPU kicks. The guest sends
about 3,600 function-call IPIs per second (cross-CPU thread wakeups) with
15,000 context switches. Every cross-vCPU wakeup therefore costs tens of
microseconds of host CPU. Fewer vCPUs keep more wakeups local.

## Results (Assassin's Creed Nexus, start scene)

Interleaved matrix, medians of four 20 s windows:

| Configuration | Host cores | Host ms/frame | Game ms/frame | FPS |
| --- | --- | --- | --- | --- |
| baseline | 1.87 | 21.3 | 16.5 | 87.5 |
| asg only | 1.81 | 20.3 | 14.6 | 89.3 |
| native skinning + cached buffers | 1.22 | 13.6 (-36%) | 7.9 (-52%) | 89.7 |
| asg + cached buffers | 1.16 | 12.9 (-39%) | 7.2 (-57%) | 89.8 |
| all three | 1.24 | 13.8 (-35%) | 7.8 (-53%) | 89.8 |
| all three, 3 vCPUs | 1.09-1.18 | 12.2-13.2 (-40%) | | 89.8 |
| all three, 2 vCPUs | 1.05-1.06 | 11.8-11.9 (-44%) | | 89.3 |

The frame rate rose to the 90 Hz cap in every optimized configuration.

## Finding 4: Unity's worker threads cost more than they save

Unity starts one job worker per spare CPU and a separate render thread. Under
translation every hand-off between them is a cross-vCPU wakeup (Finding 3),
and with skinning fixed the jobs themselves are small. Unity reads player
command-line arguments from the activity's `unity` intent extra
([Android command-line arguments](https://docs.unity3d.com/Manual/android-custom-activity-command-line.html),
[player arguments](https://docs.unity3d.com/Manual/PlayerCommandLineArguments.html)):
`-job-worker-count 0` runs jobs on the calling thread and `-force-gfx-direct`
renders on the main thread. They are not applied: they are Unity-specific and
not safe for every game (North Star fell from 76 to 53 fps with them, because
its main thread then also runs its ocean simulation), and AXRB applies no
per-game settings.

Interleaved matrix with a fixed synthetic head pose (so the game renders
continuously), medians of four 20 s windows. The native kernels and cached
buffers are on in every row except the baseline:

| Configuration | Host cores | Host ms/frame | Game ms/frame | FPS |
| --- | --- | --- | --- | --- |
| baseline | 1.89 | 22.1 | 16.6 | 86.0 |
| + `-job-worker-count 0` | 1.08 | 12.1 (-45%) | 6.5 | 89.8 |
| + `-job-worker-count 0`, 3 vCPUs | 1.10 | 12.4 (-44%) | 7.4 | 89.6 |
| + `-job-worker-count 0`, 2 vCPUs | 0.92 | 10.3 (-53%) | 6.0 | 89.5 |
| + `-job-worker-count 0 -force-gfx-direct` | 0.94 | 10.5 (-53%) | 5.3 | 89.8 |

## Finding 5: the local APIC belongs in the hypervisor

The legacy emulator's WHPX code emulates the local APIC in QEMU (Finding 3).
Upstream QEMU instead lets Hyper-V emulate it (`LocalApicEmulationMode`), and
this machine's WHPX supports that, including the APIC exit traps QEMU would
need to keep its own interrupt routing. The clock helper can now bridge the
two (`host/clock/hv_apic.h`, opt-in with `AXRB_WHPX_HV_APIC=1`):

- Hyper-V emulates every local APIC register, the APIC timer, IPIs and HLT,
  without exits to QEMU.
- Device interrupts still come from QEMU's IOAPIC and MSI model. The bridge
  catches the interrupt QEMU injects and requests it from the hypervisor APIC
  instead. MSI (edge) interrupts are acknowledged in QEMU's APIC at once;
  IOAPIC (level) interrupts are requested level-triggered, and the guest's
  EOI reaches QEMU through an EOI exit. Acknowledging MSIs only on the guest's
  EOI let QEMU's and the hypervisor's in-service registers diverge. The
  virtio-blk completion interrupt was then lost after about 15 s of gameplay,
  and all disk I/O stalled.
- The APIC register writes QEMU's routing depends on (SVR, LDR, DFR, LINT)
  and INIT/SIPI are forwarded to QEMU's model as exit traps.
- Linux rejects Hyper-V's APIC timer during its calibration check. The timer
  first delivers the periods that elapsed since calibration began, and Linux
  then falls back to the PIT. The guest kernel has VMware guest support but no
  Hyper-V support. So CPUID reports VMware, and the VMware backdoor's GETHZ
  command reports the TSC and APIC timer rates, which makes Linux skip
  calibration.
- With `AXRB_WHPX_HV_X2APIC=1` in addition, the guest also gets x2APIC: CPUID
  leaf 1, and VMware's "legacy x2APIC" vCPU flag, which Linux requires without
  interrupt remapping. APIC accesses become MSR writes the hypervisor handles
  without decoding an instruction, and an IPI is one write instead of three.

Cross-CPU wakeup cost (`out/tmp/lat/pingpong.c`: two threads pinned to
different vCPUs wake each other through a futex; idle guest):

| APIC | Round trip | Guest CPU per round trip |
| --- | --- | --- |
| QEMU (stock) | 113-130 us | 50-61 us |
| Hypervisor, xAPIC | 41-52 us | 19-29 us |
| Hypervisor, x2APIC | 27 us | 17 us |

Nexus, synthetic head pose, medians of four 20 s windows (the default row has
native skinning and cached buffers):

| Configuration | Host cores | Guest busy | Host ms/frame | FPS |
| --- | --- | --- | --- | --- |
| baseline | 1.92 | 1.49 | 22.6 | 85.3 |
| default | 1.40 | 0.90 | 15.6 (-31%) | 89.6 |
| default + hypervisor APIC | 1.18 | 0.91 | 13.3 (-41%) | 87.6 |
| + `-job-worker-count 0 -force-gfx-direct` | 1.04 | 0.75 | 11.6 (-49%) | 89.5 |
| + both | 0.86 | 0.67 | 9.6 (-58%) | 89.0 |

The hypervisor APIC halves the virtualization overhead (host cores minus
guest busy cores: 0.49 to 0.28).

Single runs vary by up to ±15% because the game's own CPU use varies between
boots, so differences below about 10% need more rounds. `session.py` refuses
to measure when the hypervisor APIC was requested but is not active; the
guest's reschedule-interrupt (`RES`) and `LOC` rates also tell the two apart.

It is the default with the clock helper (Finding 7);
`scripts/emulator/windows_android_emulator.ps1 -LocalApic` (`Qemu`,
`Hypervisor`, `HypervisorX2Apic`), `run_windows_game.ps1 -LocalApic` and the
launcher setting `localApic` override it. x2APIC measured the same as xAPIC
in both games and stays optional.

## Finding 6: North Star's remaining cost is game code

A North Star profile with the translator's perf map (`berberis.profiling=1`,
`experimental/cpu/jit_breakdown.py`) attributes most of its job worker's
translated code to Unity Burst code (`lib_burst_generated.so`): Meta's ocean
simulation (`Meta.Utilities.Environment`, [design notes](https://github.com/oculus-samples/Unity-NorthStar/blob/main/Documentation/OceanSystemDesignAndImplementation.md)),
a 128 x 128 inverse FFT on the CPU every frame, about 18% of the game's CPU.
Native, bit-identical replacements of those jobs (verified against the game's
own code running under the translator) saved 12% of host CPU per frame, but
they match one game's code, so they were removed again: AXRB applies no
per-game changes.

## Finding 7: address-space graphics once exits are cheap

With the hypervisor APIC, North Star's Vulkan command thread (about a sixth of
the game's samples, a third of them in `goldfish_pipe_read_write`) became the
largest remaining virtualization cost. The emulator's address-space graphics
transport (`hw.gltransport=asg`) passes the command stream through a shared
ring instead of a pipe write, and exit, per flush. It was within noise with
QEMU's APIC (Finding 3), but not with the hypervisor's. North Star, hypervisor
APIC in every row, medians of four 20 s windows:

| Configuration | Host cores | Host ms/frame | FPS |
| --- | --- | --- | --- |
| pipe transport | 1.62 | 18.6 | 87.0 |
| asg transport | 1.50 | 16.6 (-11%) | 90.0 |
| no accurate SIGSEGV (`ro.berberis.flags=none`) | 1.51 | 17.3 (-7%) | 86.5 |
| translator heavy-optimize mode | 1.56 | 17.7 (-5%) | 88.0 |
| 3 vCPUs | 1.62 | 19.0 (+2%) | 85.0 |

Only the transport is adopted: the translator options are within noise, and
precise SIGSEGV reporting is what games that catch their own faults need.

Both are defaults now: `windows_android_emulator.ps1 -LocalApic Auto` selects
the hypervisor APIC whenever the clock helper runs (TscCorrected), and
`-GraphicsTransport Asg` sets `hw.gltransport=asg` in the AVD before starting
it. `-LocalApic Qemu` and `-GraphicsTransport Pipe` restore the previous
behaviour; the launcher setting `localApic` passes through.

## Generic options that did not help

Measured on North Star with the new defaults, interleaved, six 20 s windows
unless noted:

| Option | Host ms/frame vs default |
| --- | --- |
| no wake interrupt when QEMU kicks a halted vCPU (`AXRB_WHPX_HV_APIC_WAKE=0`) | +3% |
| guest transparent huge pages `always` | +2% |
| 3 vCPUs (four windows) | +2% |
| x2APIC (four windows) | -2% |
| translator heavy-optimize mode (four windows) | -5%, within noise |

Host large pages for guest RAM would need the "Lock pages in memory"
privilege, which Windows does not grant by default.

## Open items

- Validate image correctness with a worn headset in real gameplay. Headless
  captures of the projection layer were black in every configuration,
  including the baseline.
- Gameplay scenes are heavier than the start scene; re-measure there.
