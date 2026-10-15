# Digitalis Windows adapter

AXRB builds the public Digitalis ARM64 optimizing translator, lite translator and interpreter at
`f5f1c90b17d0c1df9e9be41534c653a3e74bfc64` from
https://github.com/DigitalisX64/platform_frameworks_libs_binary_translation,
with AXRB's patches from `direct/patches`.
Run `scripts/build/direct_deps.ps1` before configuring Direct.

This is a native Windows adapter, not Android NativeBridge. The optimizing tier
promotes hot regions from the lite JIT, with register allocation and internal
loop linking. Promotion runs on two background threads: the guest thread keeps
running the lite translation and installs the optimized code the next time it
runs, unless an invalidation or cache reset happened meanwhile. Unsupported lite-JIT instructions use Digitalis's interpreter;
they are not silently delegated to Dynarmic. The existing AXRB process, syscall,
memory, Android framework and graphics implementations are shared by both CPUs.
Dynarmic remains the default. Select Digitalis in the launcher settings, or set
`AXRB_DIRECT_TRANSLATOR=digitalis` when invoking axrb-direct.exe directly.

The build uses NDK 29.0.14206865 Clang targeting the MSVC ABI, Visual Studio's
x64 tools and static CRT. No compiler, Android SDK or extra runtime DLL is
required on the player's machine. CMake runs the native self-test when building
the adapter, including runs with AVX and block chaining disabled, forced
optimization, optimization on the guest thread, and the lite tier alone. A
differential test compares randomized arithmetic, flags, memory loops, a loop
optimized in the background and FP min/max lanes against the interpreter. AVX/FMA/F16C
are enabled only when supported by both the CPU and Windows XSTATE support.
AVX2 uses the same OS-state check. Integer extensions are selected from CPUID,
including upstream's avoidance of slow PDEP/PEXT on pre-Zen-3 AMD processors.

The compatibility headers replace Android logging/allocation/platform glue.
`entry.asm` preserves the Windows nonvolatile registers around generated code.
The per-thread cache uses upstream memory-fault recovery maps; unmapped guest
accesses return through AXRB's signal handling. Cache invalidations are queued
by AXRB and applied between execution batches to affected guest pages. The cache
commits memory as needed, per thread, up to 1/32 of the machine's RAM (at least
Dynarmic's 256 MiB, at most 1 GiB); reaching the limit discards the thread's
translations.
Each thread's code lives in one reserved range that is writable and executable,
as Dynarmic's cache is, so installing a block needs no protection changes. The
range lies within one 4 GiB block of the address space (branch predictors keep
only the low half of a target), first-tier code grows up from its bottom and
optimized code down from its top, which keeps the code a thread spends its time
in together.
A bounded assembly dispatcher chains cached blocks without repeatedly crossing
the Windows ABI boundary; it checks interrupts and faults between blocks.
Internal optimized loops also consume a bounded execution budget. Syscall sites
are cached with the same invalidation rules as translated instructions, avoiding
repeated Windows memory queries on hot graphics and Android service calls.
Translated syscall exits return their already decoded immediate directly.
The per-thread branch lookup tables have 262,144 entries (address space of
which only the pages a thread's code reaches use memory). A game engine's main
thread translates a few hundred thousand blocks; with 65,536 entries its
returns and virtual calls kept evicting each other, and every miss left
generated code for the C++ dispatcher. Blocks that no longer count toward
optimization are in a table of their own with 16-byte entries, the only one
generated code reads.
Translated blocks jump to each other directly. A direct branch is a rel32 jump
that the owning thread points at the target's code when the target is compiled,
promoted or invalidated, and at a dispatcher stub at the start of the range
otherwise. Indirect branches probe the lookup table inline. Both forms check
the interrupt flag first. Interrupting a thread also expires the loop budget.
ARM64 ignores the top byte of an address. Clearing it costs two shifts in the
path of every address, so loads and stores that have fault recovery start
without them: a tagged pointer faults there as a non-canonical address, the
instruction joins a process-wide set and its page is translated again with the
shifts (bionic's allocator tags the address of its chunk headers, so every
process has a few such instructions). Past 2,048 of them all code is translated
with the shifts, as `AXRB_DIGITALIS_MASK_ADDRESSES=1` does from the start.
`svc #0` first tries AXRB's in-process fast syscalls (`clock_gettime`,
`gettimeofday`, futex wake) before leaving generated code.
`direct/patches/digitalis-*.patch` are applied by `direct_deps.ps1`. They add lite
JIT lowering for pairwise FMAXP/FMINP/FMAXNMP/FMINNMP and mask stale upper lanes
in 64-bit element-wise FMAX/FMIN (the differential test covers both), make the
top-byte mask conditional as described above, and let optimized conditional
branches test the stored NZCV bits directly instead of rebuilding host flags.

`build_heavy.py` generates compatibility copies outside the pinned upstream tree.
These adapt operand evaluation order to clang-cl, preserve observable ARM FP
exception flags during dead-code elimination, and allocate the upstream constant
pool below 2 GiB for signed 32-bit references. The generated code makes no SysV
native calls; the Windows ABI boundary is owned by `entry.asm`.

Developer diagnostics: `AXRB_DIGITALIS_PROFILE=1` logs per-thread compilation,
dispatch and interpreter totals. `AXRB_DIGITALIS_DISABLE_AVX=1` exercises the
portable instruction path, and `AXRB_DIGITALIS_NO_CHAIN=1` disables block chaining
for comparisons. `AXRB_DIGITALIS_HEAVY=0` disables hot-region optimization;
`force` exercises the optimizer immediately in CPU tests. The normal mode
promotes after 512 hits and accepts short successful regions as well as loops.
`AXRB_DIGITALIS_SYNC_HEAVY=1` optimizes on the guest thread instead of in the
background.
`AXRB_DIGITALIS_DISABLE_EXTENDED_CPU=1` disables the additional CPUID extensions.
`AXRB_DIGITALIS_CACHE_BITS=16` selects the earlier 65,536-entry lookup table for
comparisons (supported range 12–20). `AXRB_DIGITALIS_HEAVY_MIN_INSNS=20` restores
the older region-size heuristic; the default is 1.
`AXRB_DIGITALIS_DUMP=<directory>` writes every region's host code there as
`<guest pc>-<lite|opt>-<host address>.bin`, and with `AXRB_DIRECT_PROFILE` set,
`AXRB_DIRECT_PROFILE_JIT=1` adds `<report>.jit` and `<report>.code`: every sample
in translated code with its region, and those regions' host code, for an
instruction-level profile.
These switches are not set by the launcher. The deterministic
ARM64 `direct/tests/art/translator_benchmark.c` times one construct per line
(calls, indirect branches, memory copies, atomics, floating point), separately
from game FPS.

The vendored CPU-state declaration in `compat/native_bridge_support` comes from
AOSP frameworks/libs/native_bridge_support, main branch, downloaded 2026-10-06:
https://android.googlesource.com/platform/frameworks/libs/native_bridge_support/+/refs/heads/main/guest_state/include/native_bridge_support/arm64/guest_state/guest_state_cpu_state.h
It retains its upstream Apache-2.0 header. Digitalis/Berberis sources retain
their original Apache-2.0 copyright headers. See the launcher's third-party
notices and `licenses/Apache-2.0.txt`.
