#pragma once

#include <string>

namespace axrb {

// The Android system root extracted from the SDK system image (see
// out/sysimg/mkroot.py): mounted read-only at /, with its symlinks.
bool sysroot_init(const std::wstring& root);
// Synthesized /proc, /sys and /dev.
void proc_init();
// Default location of the extracted system root and of AXRB Direct's
// persistent data (%LOCALAPPDATA%\AXRB Runtime\direct).
std::wstring default_sysroot();
std::wstring data_root();

}  // namespace axrb
