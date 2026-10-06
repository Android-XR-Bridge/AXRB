# Digitalis performance port

Source: [MIXIDtheSilly/Refract](https://github.com/MIXIDtheSilly/Refract),
revision `8a03ae4`, MIT attribution in `licenses/Refract-MIT.txt`.
Digitalis/Berberis retains its Apache-2.0 notices.

`digitalis-axrb-performance.patch` applies to Digitalis binary_translation
`9e7f4407e0e32a582fe2a56f87492443ca55d6fa`. It combines direct block chaining,
host call/return prediction and the
guest system-library redirection needed for Digitalis audio compatibility.
The upstream return-prediction patch references unpublished cross-counter
instrumentation; the combined patch applies the call/return changes without
requiring that diagnostic code. The base already has the safe heavy-tier TBI
masking restored by Refract's performance-fix patch.

The companion `native-bridge-tls-compat.patch` targets native_bridge_support
`ffd3f17ebbb244c01c274b6f527d016efc393813`. It obtains the host Bionic TLS
pointer through a translator callback instead of reading a private host
`pthread_internal_t` field using the guest build's layout. Both patches must
be used together and are included in the bundle's source provenance.

`bionic-api36-compat.patch` restores the private pthread/TLS layouts and their
associated initialization, libgen and tracing code from Android 16 r1
(`d9d7d57e286b7d840f0aa494848a4202d611d13d`) onto the pinned QPR2 Bionic tree
(`f22516cbc67e81c13cf943ce02f01b8929a98726`). Digitalis shares these structures
with the host libc; matching the public API level alone is insufficient.
The installer therefore accepts only AXRB's tested Google API 36 image,
build `BE2A.250530.026.F3/13894323`, and leaves other images unchanged in auto
mode. Revalidate the private ABI before adding another image.

Refract's lite load/store shortcut skips top-byte masking unless explicitly
enabled. It crashed the upstream `Arm64MemoryDifferentialFuzz.TaggedBaseAddressIsIgnored`
test in the AXRB build, so AXRB retains unconditional masking for tagged-pointer
correctness. The original optimization patch remains available for review.

Block chaining and return prediction are enabled by default. For translator
diagnostics, `berberis.chain=0` and `berberis.retpred=0` disable them independently
on Android before starting an app (host equivalents: `BERBERIS_CHAIN=0` and
`BERBERIS_RETPRED=0`). AXRB also sets `berberis.gearup_min_insns=0` on game launch.

The individual upstream patches are retained for review. The SIMD-address,
heavy-readonly-TBI and pairwise-minmax candidates are **not applied** by the
build: Refract's distribution manifest identifies only its performance-fix,
block-chaining and return-prediction changes as shipped optimizations.

On Linux/WSL, sync Digitalis's AOSP manifest and run:

```sh
bash /path/to/AXRB/tools/translator/build_digitalis.sh /path/to/aosp 4
```

This runs ARM64 host tests before building and verifying the complete bundle.
It builds every installed path in `BERBERIS_DISTRIBUTION_ARTIFACTS_ARM64`;
upstream's filtered host-module list alone does not build the guest libraries.
The bundle is `digitalis/dist/digitalis-prebuilts` in the AOSP tree; source
revisions and patch checksum are recorded alongside it. The completed AXRB
archive is `digitalis/dist/digitalis-prebuilts-axrb.tar.gz`, with a sibling
`.sha256` file. It includes source provenance and license notices.
Use a Linux filesystem
on the second drive for the source, temporary files, caches and build output.
Do not point a fresh AOSP checkout at a nearly full system drive.
