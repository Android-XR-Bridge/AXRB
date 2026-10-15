#pragma once

#include "kernel.h"

#include <string>
#include <vector>

namespace axrb {

// What the kernel's execve does for an arm64 ELF: maps the program and its
// interpreter (the Android linker), builds the initial stack (argc, argv,
// envp, auxv) and returns the first register state.
int64_t exec_program(GuestThread& thread, const std::string& path, const std::vector<std::string>& argv,
                     const std::vector<std::string>& envp, CpuContext& start);

}  // namespace axrb
