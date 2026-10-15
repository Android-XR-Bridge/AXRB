Experimental native x86_64 Android/ART backend
2026-10-07 -- native ART, ARM NativeBridge, North Star scene milestone

Status
======
Real Android x86_64 linker64, Bionic, dalvikvm64 and libart execute inside a
Windows process without a hypervisor, Linux kernel or adb. ART executes native
x86 instructions; ARM libraries now use Digitalis through a JNI/NativeBridge adapter.
The managed regression passes with arithmetic, Java threads/monitors/TLS,
20,000 JNI round trips with callbacks into Java, concurrent-worker GC request,
eight additional allocation/GC cycles, Java exceptions and clean shutdown.
The standard ART NativeBridge v8/System.load path also passes for the ARM test
library, including JNI_OnLoad, JavaVM GetEnv and dynamic RegisterNatives.
ART is in interpreter mode (-Xint) with an imageless boot class path. This is
not JIT support or an Android system boot. North Star now reaches its rendered
dock/start scene using the embedded ARM Bionic service. See GAME-STATUS.txt for
the current evidence, reproducible launch command and remaining limitations.

The launcher, installed ARM runtime and version 2.0.0 setup are unchanged.
This target is separate, developer-only and not included in release packages.
Only trusted developer binaries may be used. It is not a sandbox.

Implementation/change list
==========================
main.cpp
  Validated ELF64 loading, Linux startup stack and auxv, actual Android linker
  execution, and instruction-boundary syscall instrumentation using pinned Zydis.
  The linker handles normal dynamic relocations and dependencies itself.
  Windows-backed Linux syscall subset: file reads/stat/mmap, protection/unmap,
  clocks, random, identity, pthread clone, futex, blocked signal waiting/queueing.
  Small anonymous mappings can occupy a reserved low-address arena for ART's
  compressed object references. PROT_NONE reservations do not commit RAM.
  Private anonymous MADV_DONTNEED now actually discards/zeros pages. Returning
  an error for this everywhere previously left ART with stale GC forwarding data.
  Read-only MAP_SHARED uses real Windows section views. Writable shared/JIT aliases
  are explicitly unsupported, rather than silently implemented as private copies.
  Per-thread guest FS bases and native return stubs accommodate Windows context
  restoration. Windows GS/TEB is preserved. FSGSBASE support is required.
  Guest pthreads use Windows threads, separate Bionic TLS, inherited signal masks,
  child-TID clear/wake, and host-stack restoration before thread exit.
  Futex wait/wake, bitsets, requeue and bounded/saturating timeouts use Windows
  events. Blocking waits release the shared kernel-state mutex.
  rt_sigtimedwait and blocked thread-directed signals use pending queues/events.
  Unsupported asynchronous signal-handler delivery returns ENOSYS.
  Executable anonymous allocations and writable executable mappings are refused.
  Unknown syscalls produce diagnostics and ENOSYS; missing behavior is not faked.

files.cpp / files.h
  Read-only Android sysroot with canonical Linux symlinks, explicit file mounts,
  Linux x86_64 stat layout and generated /proc/self/maps, /proc/self/stat and
  linker configuration. Host path containment is checked for sysroot lookups.
  Visible APEX namespaces delegate to the default namespace. No intermediate
  system namespace is exported: Android NativeLoader's default fallback avoids
  loading a second libc with incompatible initial-exec TLS for JNI libraries.
  The configuration is deliberately permissive for this isolated prototype;
  it does not reproduce Android namespace security/isolation policy.

code_ranges.py / prepare_sysroot.py
  Extract a separate developer sysroot from an already installed/licensed SDK
  x86_64 image. Detect compressed APEX containers by contents and canonical names
  from apex_manifest.pb. Validate required x86_64 binaries and record SHA256s.
  Generate hash-bound instruction ranges from ELF mini-debug function symbols
  so embedded ART jump tables are not decoded as instructions. ELF files remain
  unchanged. Stale, overlapping, truncated or out-of-section metadata is rejected.
  Metadata describes trusted toolchain output, not authenticated executable code.

tls_bridge.cpp / tls_bridge.h
  Native out-of-line bridges virtualize supported GS memory instructions through
  Windows per-thread TLS. Scratch registers, flags and the SysV red zone are
  preserved. Handles indexed/32-bit addresses, storing original RSP and GS PUSH.
  Unsupported instructions fail with a diagnostic. No guest WRGSBASE is allowed.

CMakeLists.txt / tests / run_art_probe.py
  Standalone executable and NDK fixtures, optional real-sysroot integration tests,
  managed DEX/JNI build harness, and reproducible CTest commands below.
  Regression output requires both a success marker and exit status zero.

Validated on this PC
====================
All ten CTest suites pass (see bridge-test-results.txt):
  native-x64-boundary: ABI/startup, native FS/virtual GS, register/flag/stack
    preservation, syscall/data distinction, memory protection/unmap/discard,
    errors, clock, malformed ELF and invalid code-range metadata rejection.
  native-x64-sysroot: four APEX/container/manifest parser checks.
  native-x64-bionic: real dynamic linker, libc malloc/free, TLS, clock,
    dlopen/dlsym of libm and clean exit.
  native-x64-pthread: four workers, independent TLS/errno, mutex/condition,
    join, timed wait, inherited blocked signal, delivery and timed signal wait.
  native-x64-art: real managed arithmetic, four Java workers, 20,000 JNI calls
    and Java callbacks, monitors/TLS, allocation/GC with retained data checks,
    exception construction/catch and clean shutdown.

The managed success line is:
  NATIVE_X64_ART_PASS arithmetic=49995000 threads=20000 JNI callbacks and GC
ART logs confirm reclamation of previous large objects during repeated GC.
Additional suites:
  native-x64-arm-jni: explicit registration of ARM functions into native ART;
    mixed integer/FP register and stack arguments, nested ARM/Java callbacks,
    strings, arrays, exceptions, unsupported-JNI rejection and four Java workers.
  native-x64-nativebridge: the same checks using ordinary System.load and the
    standard Android NativeBridge v8 interface, plus JNI_OnLoad, cached JavaVM
    GetEnv across threads and dynamically registered methods.
  native-x64-arm-boundary: malformed/truncated/wrong-architecture ELF, protocol
    version, missing symbol, invalid code pointer and cross-thread handle errors.
  axrb_digitalis_windows and axrb_digitalis_differential: existing engine checks.
The standard NativeBridge test passed ten consecutive runs without tracing.
These are correctness probes, not a throughput benchmark or a game-FPS claim.
The default CI job runs the boundary/parser suites; real Android integration is
opt-in because it needs a separately prepared, licensed developer SDK image.

Development commands (repository root)
======================================
  cmake -S direct/x64 -B out/native-x64 -G "Visual Studio 17 2022" -A x64
  cmake --build out/native-x64 --config Release --parallel
  ctest --test-dir out/native-x64 -C Release --output-on-failure

Prepare an existing SDK image without running an emulator/kernel:
  python direct/x64/prepare_sysroot.py IMAGE_SYSTEM_IMG NEW_OUTPUT_DIRECTORY
Enable real Android integration tests using an absolute sysroot path:
  cmake -S direct/x64 -B out/native-x64 -DAXRB_X64_SYSROOT=ABSOLUTE_SYSROOT_PATH
  cmake --build out/native-x64 --config Release --parallel
  ctest --test-dir out/native-x64 -C Release --output-on-failure
  ctest --test-dir out/native-x64 -C Release --output-on-failure --repeat until-fail:10 -R "native-x64-(art|pthread)$"

Configurable developer dependencies:
  AXRB_NDK defaults to %LOCALAPPDATA%/Android/Sdk/ndk/29.0.14206865
  AXRB_ANDROID_BUILD_TOOLS defaults to SDK build-tools/36.0.0
  javac/java must be on PATH for the managed integration test.
No developer SDK dependency is added to the installed launcher.

Local artifacts:
  out/native-x64/Release/axrb-native-x64.exe
  out/native-x64/art-probe.out and art-probe.err
  out/native-x64/Testing/Temporary/LastTest.log
  out/native-x64/milestone.json
  out/deps/aosp-x64-native/.axrb-x64-sysroot.json
AXRB_X64_TRACE=1 enables verbose syscall/file diagnostics in a developer run.
The isolated runner has a 10-second execution watchdog; the ART harness also
requires clean process completion. Do not use the current runner for long games.

Earlier milestone limits (superseded where noted in GAME-STATUS.txt)
========================
* ARM NativeBridge v8 now handles dependency-free ARM64 ELF with local RELATIVE
  relocations and the tested JNI subset. Full game libraries still need ARM
  DT_NEEDED/linker/libc/TLS, constructors, complete JNI (including variadic/V
  forms), native thread attachment, CriticalNative and callback ABI coverage.
  Current pools allow 64 JNI trampolines and 16 nested engines per Windows thread.
  This is a bounded integration prototype, not broad NativeBridge compatibility.
* JIT/generated code and coherent writable shared executable aliases are absent.
  File-backed private mappings are snapshots; writeback and general file I/O,
  networking, Binder, framework services and x86 graphics/audio bridges remain.
* Linux asynchronous signal frames, SIGSEGV delivery to ART, implicit null/stack
  checks, alternate-stack delivery and sigreturn need full implementation/tests.
  The tested Java exception is explicit; implicit fault recovery is not proven.
* Current syscall gates use Windows exception dispatch and a global kernel-state
  mutex. This is a correctness scaffold; hot syscall overhead still needs removal.
* Host exception handling currently uses the guest's active stack. Discard of that
  stack is refused, and unsupported stack/unwind/CET cases remain. A separate host
  syscall stack and a fully specified ABI transition are needed for production.
* Clone copies the base Windows CONTEXT (including SSE) but not arbitrary extended
  AVX/XSTATE payload. Wider SIMD inheritance and signal contexts need coverage.
* Some unmaps retain address reservations/section views/stub blocks until process
  exit. Long-running allocation/address-reuse behavior is not yet complete.
* No full Linux scheduling/priority/affinity, directory enumeration, real-time
  signal queue semantics or full /proc implementation is claimed. ART emits
  diagnostics for unsupported optional calls during the passing regression.
* Require FSGSBASE on Windows; no older-host fallback. The audited assembly
  red-zone test passes here, but Windows does not promise SysV red-zone safety
  under arbitrary OS/debugger/unwind use. This needs a stronger boundary.
* Broad application compatibility and a performance comparison require rebuilding
  the framework bootstrap/bridges for x86_64, completing ARM NativeBridge, actual Pinball,
  North Star and Manta tests, and identical scenes/settings on both backends.
  Do not change launcher defaults or claim emulator speed parity from this probe.

Primary implementation references
=================================
https://android.googlesource.com/platform/bionic/+/refs/heads/main/linker/linker_main.cpp
https://android.googlesource.com/platform/bionic/+/refs/heads/main/libc/bionic/__libc_init_main_thread.cpp
https://android.googlesource.com/platform/art/+/refs/heads/main/libnativeloader/native_loader_namespace.cpp
https://android.googlesource.com/platform/art/+/refs/heads/main/libnativebridge/README.md
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
https://learn.microsoft.com/en-us/cpp/build/stack-usage
https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention

Zydis/Zycore reuse the existing pinned third-party sources/licenses in
third_party/dynarmic/externals/{zydis,zycore}. Digitalis is now statically linked
for ARM library execution; ART itself still executes natively. The NativeBridge
ABI header comes from the existing pinned third_party/digitalis checkout.

Earlier milestone validation: ART and pthread suites each passed ten consecutive
runs. Current bridge results: out/native-x64/bridge-test-results.txt and
nativebridge-probe.{out,err}. Implementation changes: NATIVE-BRIDGE.txt.
