/* hangdump: every thread's native call stack in a running process, named
   from its PDB. For a guest that freezes inside host code, where the
   sampler (which needs the same locks) cannot report anything.

       hangdump <pid | exe name>

   Build: cl /nologo /O2 /EHsc tools\hangdump.cpp dbghelp.lib */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

static DWORD find_pid(const char* wanted) {
    if (std::strspn(wanted, "0123456789") == std::strlen(wanted)) return (DWORD)std::strtoul(wanted, nullptr, 10);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 entry{sizeof(entry)};
    DWORD pid = 0;
    for (BOOL ok = Process32First(snap, &entry); ok; ok = Process32Next(snap, &entry))
        if (_stricmp(entry.szExeFile, wanted) == 0) pid = entry.th32ProcessID;
    CloseHandle(snap);
    return pid;
}

int main(int argc, char** argv) {
    DWORD pid = find_pid(argc > 1 ? argv[1] : "qb-test.exe");
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        std::printf("cannot open process %lu\n", pid);
        return 1;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{sizeof(entry)};
    for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        SuspendThread(thread);
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        GetThreadContext(thread, &context);
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        std::printf("thread %lu\n", entry.th32ThreadID);
        for (int depth = 0; depth < 40; ++depth) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                !frame.AddrPC.Offset)
                break;
            char buffer[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            IMAGEHLP_LINE64 line{sizeof(line)};
            DWORD line_displacement = 0;
            char module[MAX_PATH] = "?";
            DWORD64 base = SymGetModuleBase64(process, frame.AddrPC.Offset);
            if (base) GetModuleBaseNameA(process, (HMODULE)base, module, MAX_PATH);
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line))
                    std::printf("  %-14s %s  (%s:%lu)\n", module, symbol->Name,
                                std::strrchr(line.FileName, '\\') ? std::strrchr(line.FileName, '\\') + 1 : line.FileName,
                                line.LineNumber);
                else
                    std::printf("  %-14s %s+%llx\n", module, symbol->Name, (unsigned long long)displacement);
            } else {
                std::printf("  %-14s %llx\n", module, (unsigned long long)frame.AddrPC.Offset);
            }
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }
    CloseHandle(snap);
    return 0;
}
