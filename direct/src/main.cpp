// AXRB Direct: runs Android arm64 programs on Windows without an emulator.
//
// The guest's own bionic, linker and system libraries (from the Android SDK
// system image) run under Dynarmic; this process plays the Linux kernel
// underneath them and the Android services around them.

#include "app.h"
#include "audio.h"
#include "ndk.h"
#include "cpu.h"
#include "exec.h"
#include "files.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "media.h"
#include "net.h"
#include "props.h"
#include "vk_host.h"
#include "profiler.h"
#include "sysroot.h"
#include "vfs.h"

#include <windows.h>
#include <timeapi.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace axrb;

namespace {

struct Options {
    std::wstring sysroot;
    std::wstring data;
    std::vector<std::string> env;
    std::map<std::string, std::string> props;
    std::string program;
    std::vector<std::string> args;
    AppOptions app;
    bool app_mode = false;
    bool low_memory = false;
    bool host_graphics = false;
    std::vector<std::pair<std::string, std::wstring>> read_only_mounts;
};

void usage() {
    std::fprintf(stderr,
                 "usage: axrb-direct [--sysroot DIR] [--data DIR] [--env K=V]... [--prop K=V]... PROGRAM [ARGS...]\n"
                 "       axrb-direct [--sysroot DIR] [--data DIR] --apk APK [--obb DIR] [--assets LIST] [--activity NAME]\n"
                 "  PROGRAM is a guest path (/system/bin/...) or a host file, which is mounted read-only at\n"
                 "  /data/local/tmp/axrb-host.\n");
    std::fprintf(stderr, "  --low-memory reserves low guest address ranges for ART's compressed Java references.\n");
    std::fprintf(stderr, "  --mount-ro GUEST=HOST mounts a host file or directory read-only.\n");
}

bool parse(int argc, wchar_t** argv, Options& o) {
    int i = 1;
    for (; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--sysroot") o.sysroot = next();
        else if (a == L"--data") o.data = next();
        else if (a == L"--env") o.env.push_back(narrow(next()));
        else if (a == L"--low-memory") o.low_memory = true;
        else if (a == L"--host-graphics") o.host_graphics = true;
        else if (a == L"--mount-ro") {
            std::wstring mapping = next();
            size_t eq = mapping.find(L'=');
            if (eq == std::wstring::npos || eq == 0 || mapping[0] != L'/' || eq + 1 == mapping.size()) return false;
            o.read_only_mounts.emplace_back(narrow(mapping.substr(0, eq)), mapping.substr(eq + 1));
        }
        else if (a == L"--apk") {
            o.app.apk = next();
            o.app_mode = true;
        } else if (a == L"--obb") o.app.obb_dir = next();
        else if (a == L"--assets") o.app.asset_list = next();
        else if (a == L"--activity") o.app.activity = narrow(next());
        else if (a == L"--prop") {
            std::string kv = narrow(next());
            size_t eq = kv.find('=');
            if (eq != std::string::npos) o.props[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else if (a == L"--help" || a == L"-h") {
            return false;
        } else {
            break;
        }
    }
    if (o.app_mode) return i >= argc;
    if (i >= argc) return false;
    o.program = narrow(argv[i]);
    for (++i; i < argc; ++i) o.args.push_back(narrow(argv[i]));
    return true;
}

void ensure_directory(const std::wstring& path) {
    // CreateDirectory on each component; existing ones are fine.
    for (size_t at = path.find(L'\\', 3); ; at = path.find(L'\\', at + 1)) {
        CreateDirectoryW(path.substr(0, at).c_str(), nullptr);
        if (at == std::wstring::npos) break;
    }
}

std::vector<std::string> default_environment() {
    return {
        "PATH=/product/bin:/apex/com.android.runtime/bin:/system/bin:/system/xbin",
        "ANDROID_ROOT=/system",
        "ANDROID_DATA=/data",
        "ANDROID_STORAGE=/storage",
        "EXTERNAL_STORAGE=/sdcard",
        "ANDROID_ART_ROOT=/apex/com.android.art",
        "ANDROID_I18N_ROOT=/apex/com.android.i18n",
        "ANDROID_TZDATA_ROOT=/apex/com.android.tzdata",
        "HOME=/data/local/tmp",
        "TMPDIR=/data/local/tmp",
        "LANG=en_US.UTF-8",
    };
}

struct MainStart {
    GuestThread* thread;
    CpuContext context;
    int exit_code = 0;
};

DWORD WINAPI main_thread(void* param) {
    auto* start = static_cast<MainStart*>(param);
    GuestThread& thread = *start->thread;
    set_current_thread(&thread);
    attach_cpu(thread);
    load_context(thread, start->context);
    try {
        run_guest(thread, false);
    } catch (const ThreadExit& exit) {
        start->exit_code = exit.code;
    }
    finish_thread(thread);
    detach_cpu(thread);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        usage();
        return 2;
    }
    log_init();
    profiler_start();
    timeBeginPeriod(1);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    if (!address_space().init(options.low_memory)) return 3;
    install_fault_handler();
    net_init();

    std::wstring sysroot = options.sysroot.empty() ? default_sysroot() : options.sysroot;
    if (!sysroot_init(sysroot)) return 4;
    proc_init();

    // Persistent guest storage: /data and the shared /storage/emulated/0.
    std::wstring data = options.data.empty() ? data_root() : options.data;
    ensure_directory(data + L"\\data\\local\\tmp");
    ensure_directory(data + L"\\sdcard");
    vfs().add_mount("/data", data + L"\\data", false);
    vfs().add_mount("/storage/emulated/0", data + L"\\sdcard", false);
    for (const auto& [guest, host] : options.read_only_mounts) {
        DWORD attributes = GetFileAttributesW(host.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            LOGE("main", "read-only mount does not exist: %s", narrow(host).c_str());
            return 4;
        }
        vfs().add_mount(guest, host, true, !(attributes & FILE_ATTRIBUTE_DIRECTORY));
    }
    LOGI("main", "guest data in %s", narrow(data).c_str());

    // AXRB's runtime hands eye images to the host bridge through the GPU-share
    // layer when it is active in our Vulkan instance.
    if (vulkan_prepare_gpu_share()) options.props.emplace("debug.axrb.gpu_share", "1");
    props_init(sysroot, options.props);
    if (options.host_graphics) {
        register_named_function("art.stop_requested", [](HostCall& call) {
            call.ret(session_stop_requested());
        });
        art_windows_init();
        art_media_init();
        art_media_codec_init();
        audio_init();
        vulkan_init();
    }

    Process& proc = process();
    proc.pid = 4242;
    proc.next_tid = proc.pid + 1;
    proc.cwd = "/";

    std::vector<std::string> env = default_environment();
    std::string guest_program = options.program;
    if (options.app_mode) {
        options.app.data_root = data;
        if (!app_prepare(options.app, guest_program, env)) return 7;
    }

    // A host file is mounted next to the guest's scratch space.
    std::wstring host_program = widen(options.program);
    DWORD attributes = GetFileAttributesW(host_program.c_str());
    if (!options.app_mode && attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        options.program[0] != '/') {
        wchar_t full[MAX_PATH * 4];
        GetFullPathNameW(host_program.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
        std::wstring path = full;
        size_t slash = path.find_last_of(L"\\/");
        vfs().add_mount("/data/local/tmp/axrb-host", path.substr(0, slash), true);
        guest_program = "/data/local/tmp/axrb-host/" + narrow(path.substr(slash + 1));
        proc.cwd = "/data/local/tmp";
    }

    auto* thread = new GuestThread();
    thread->tid = proc.pid;
    std::string main_name = options.app_mode ? process().package : guest_program.substr(guest_program.rfind('/') + 1);
    thread->name = main_name.size() > 15 ? main_name.substr(main_name.size() - 15) : main_name;
    register_main_thread(thread);

    fds().install_at(0, std::make_shared<ConsoleFile>(0), false);
    fds().install_at(1, std::make_shared<ConsoleFile>(1), false);
    fds().install_at(2, std::make_shared<ConsoleFile>(2), false);

    for (auto& e : options.env) env.push_back(e);
    std::vector<std::string> args = {options.app_mode ? process().package : guest_program};
    args.insert(args.end(), options.args.begin(), options.args.end());

    MainStart start{thread, {}, 0};
    // exec maps the program; it needs a cpu-less thread context only for
    // file access, so it runs here before the guest thread starts.
    set_current_thread(thread);
    if (int64_t e = exec_program(*thread, guest_program, args, env, start.context)) {
        LOGE("main", "exec %s failed: %lld", guest_program.c_str(), (long long)e);
        return 5;
    }
    set_current_thread(nullptr);

    // The guest's main thread gets the same large host stack as every other
    // guest thread.
    HANDLE h = CreateThread(nullptr, 64ull << 20, main_thread, &start, STACK_SIZE_PARAM_IS_A_RESERVATION, &thread->host_tid);
    if (!h) return 6;
    DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &thread->host_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    // The main thread called exit() without exit_group: Linux keeps the
    // process while other threads live; Android programs never rely on it.
    LOGI("main", "main thread exited (%d)", start.exit_code);
    log_flush();
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(start.exit_code & 0xff));
    return start.exit_code;
}
