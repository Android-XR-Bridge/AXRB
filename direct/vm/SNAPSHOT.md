# Androitainer snapshot

`direct/vm` is a copy of the managed runtime and framework modules of
Androitainer (`../androitainer`), taken from commit
`eeb1963eb1765aa4d54e2e878ae4d53a9441a2cd` plus that tree's uncommitted work as
of 2026-10-02:

| here | Androitainer |
|---|---|
| `managed/` | `src/managed` (DEX interpreter, core library, framework Java) |
| `diagnostics/`, `package/`, `manifest/`, `resources/`, `framework-host/` | `src/<same>` |
| `vfs/` | `src/vfs` (re-implemented on AXRB Direct's guest file system) |
| `tools/` | `tools/build-bootclasspath.ps1`, `build-framework-res.ps1`, `toolchain.json` |

`package/src/mapped_file.cc` is `MappedFile` from Androitainer's `src/elf`; the
rest of the ELF loader is not used (AXRB Direct runs the Android linker itself).
Changes made here for AXRB Direct are listed in the git history of this directory.
