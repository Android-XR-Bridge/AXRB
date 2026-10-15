// An Android application session.
//
// Host side of the launcher (direct/guest/axrb_app.c): the guest main thread
// enters "axrb.app_main", which creates the managed runtime on that thread and
// runs ActivityThread.main -- so the app's main thread, its Java threads and
// every thread its native code starts are all guest threads, and Java code,
// JNI and native code interleave on them exactly as on a device.
//
// Package layout in the guest, as the package manager would install it:
//   /data/app/~~axrb==/<pkg>-1/base.apk         the APK, read in place
//   /data/app/~~axrb==/<pkg>-1/lib/arm64/       its native libraries (extracted once)
//   /data/data/<pkg>  (= /data/user/0/<pkg>)    private storage, persistent
//   /sdcard/Android/obb/<pkg>                   the game's downloaded files, read-only
//   /sdcard/Android/data/<pkg>                  external app storage, persistent

#include "app.h"

#include "audio.h"
#include "vk_host.h"
#include "ndk.h"

#include "cpu.h"
#include "files.h"
#include "jni.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "sysroot.h"
#include "vfs.h"

#include "arun/framework_host.h"
#include "arun/manifest.h"
#include "arun/vm/class_linker.h"
#include "arun/vm/heap.h"
#include "arun/vm/natives.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"
#include "arun/vfs.h"
#include "arun/zip_reader.h"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

namespace axrb {
namespace vm = arun::vm;

namespace {

struct Session {
    AppOptions options;
    arun::manifest::Manifest manifest;
    std::shared_ptr<arun::pkg::ZipReader> apk;
    std::string package;
    std::string activity;
    std::string install_dir;  // guest
    std::string guest_apk;
    std::string lib_dir;
    std::string data_dir;
    std::wstring boot_dir;    // host directory of arun-boot.dex and arun-framework-res.apk
};

Session g_session;

// Session end: one manual-reset event, named when the launcher asked for it
// (AXRB_DIRECT_STOP_EVENT) so it can be set from outside the process.
HANDLE stop_event() {
    static HANDLE event = [] {
        wchar_t name[256];
        DWORD n = GetEnvironmentVariableW(L"AXRB_DIRECT_STOP_EVENT", name, static_cast<DWORD>(std::size(name)));
        return CreateEventW(nullptr, TRUE, FALSE, n && n < std::size(name) ? name : nullptr);
    }();
    return event;
}

// Runs on a Java daemon thread for the whole session; returns once the
// session should end, and ActivityThread then finishes the activity.
void ActivityThread_nativeWaitForStopRequest(vm::Thread* self, vm::Frame*, vm::JValue*) {
    HANDLE event = stop_event();
    {
        vm::ScopedBlocking blocking(self);
        if (event) WaitForSingleObject(event, INFINITE);
        else Sleep(INFINITE);  // no stop request can arrive
    }
    request_session_end("stop requested");
}
std::unique_ptr<vm::Runtime> g_runtime;
std::unique_ptr<arun::fw::FrameworkHost> g_framework;
arun::vfs::FileSystem g_java_fs;

std::wstring exe_directory() {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

std::wstring find_beside_exe(const std::wstring& relative_candidates) {
    std::wstring dir = exe_directory();
    for (const wchar_t* prefix : {L"\\", L"\\..\\", L"\\..\\..\\"}) {
        std::wstring candidate = dir + prefix + relative_candidates;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            wchar_t full[MAX_PATH * 4];
            GetFullPathNameW(candidate.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
            return full;
        }
    }
    return {};
}

bool ensure_dir(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

// Native libraries are compressed in most APKs; the linker needs files.
bool extract_libraries(const arun::pkg::ZipReader& zip, const std::wstring& out_dir) {
    ensure_dir(out_dir);
    int extracted = 0, kept = 0;
    for (const auto& entry : zip.entries()) {
        const std::string prefix = "lib/arm64-v8a/";
        if (entry.is_directory || entry.name.rfind(prefix, 0) != 0) continue;
        std::string name = entry.name.substr(prefix.size());
        if (name.empty() || name.find('/') != std::string::npos) continue;
        std::wstring target = out_dir + L"\\" + widen(name);
        WIN32_FILE_ATTRIBUTE_DATA data{};
        std::wstring stamp = target + L".crc";
        if (GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &data) &&
            ((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow) == entry.uncompressed_size) {
            std::ifstream in(stamp);
            uint32_t crc = 0;
            if (in >> crc && crc == entry.crc32) {
                ++kept;
                continue;
            }
        }
        auto bytes = zip.Extract(entry);
        if (!bytes.ok()) {
            LOGE("app", "cannot extract %s: %s", entry.name.c_str(), bytes.status().ToString().c_str());
            return false;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        if (!out) {
            LOGE("app", "cannot write %s", narrow(target).c_str());
            return false;
        }
        std::ofstream(stamp) << entry.crc32;
        ++extracted;
    }
    LOGI("app", "native libraries: %d extracted, %d up to date", extracted, kept);
    return true;
}

std::vector<uint8_t> read_host_file(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// --- the VM ----------------------------------------------------------------------

void log_sink(int priority, std::string_view tag, std::string_view message) {
    static const char kLevels[] = "??VDIWEF";
    std::string line;
    line += priority >= 0 && priority < 8 ? kLevels[priority] : '?';
    line += '/';
    line += tag;
    line += "(java): ";
    line += message;
    if (line.empty() || line.back() != '\n') line += '\n';
    log_raw(line.data(), line.size());
}

[[noreturn]] void fail_with_exception(vm::Runtime* rt, vm::Thread* self, const char* what) {
    std::string text = self->IsExceptionPending() ? rt->DescribeException(self, self->exception()) : "(no exception)";
    fatal("%s failed: %s", what, text.c_str());
}

struct ThreadStart {
    void (*entry)(void*);
    void* arg;
    HANDLE started;  // set once the new thread runs: Thread.start() waits for it
};

bool spawn_java_thread(void (*entry)(void*), void* arg) {
    GuestThread* guest = current_thread();
    if (!guest) return false;
    HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto* start = new ThreadStart{entry, arg, started};
    vm::Thread* self = vm::Thread::Current();
    bool runnable = self && self->state() == vm::ThreadState::kRunnable;
    if (runnable) self->TransitionFromRunnable(vm::ThreadState::kNative);
    uint64_t r = call_guest(*guest, guest_api().spawn_thread, {reinterpret_cast<uint64_t>(start), 0});
    // Like a device, where the new thread is scheduled right away: applications
    // hand work to a thread they just started and expect it to be listening
    // within the frame (Unity's render thread and the first surface).
    if (r == 0) WaitForSingleObject(started, 2000);
    if (runnable) self->TransitionToRunnable();
    if (r != 0) {  // otherwise the new thread owns (and closes) the event
        CloseHandle(started);
        delete start;
        return false;
    }
    return true;
}

// AXRB_DIRECT_DUMP=<seconds>: periodically log every thread (guest threads and
// what they block in, Java stacks) -- for finding where an app is stuck.
void start_dump_thread() {
    const char* every = std::getenv("AXRB_DIRECT_DUMP");
    if (!every || std::atoi(every) <= 0) return;
    const int seconds = std::atoi(every);
    std::thread([seconds] {
        for (;;) {
            Sleep(static_cast<DWORD>(seconds) * 1000);
            std::string out = "thread dump:\n";
            {
                std::lock_guard<std::mutex> held(process().threads_lock);
                for (auto& [tid, t] : process().threads) {
                    const char* why = t->blocked_in.load();
                    out += "  guest " + std::to_string(tid) + " \"" + t->name + "\"" +
                           (why ? std::string(" blocked in ") + why : std::string(" running")) + "\n";
                }
            }
            if (g_runtime) out += g_runtime->DumpThreads();
            log_raw(out.data(), out.size());
        }
    }).detach();
}

int run_app(GuestThread& main_thread) {
    Session& s = g_session;
    std::vector<uint8_t> boot = read_host_file(s.boot_dir + L"\\arun-boot.dex");
    if (boot.empty()) fatal("missing %s\\arun-boot.dex", narrow(s.boot_dir).c_str());

    vm::RuntimeOptions ro;
    ro.boot_class_path.push_back(vm::BootDex{"arun-boot.dex", std::move(boot)});
    ro.log_sink = log_sink;
    ro.file_system = &g_java_fs;
    ro.properties.emplace_back("user.language", "en");
    ro.properties.emplace_back("user.region", "US");
    ro.properties.emplace_back("java.library.path", s.lib_dir);
    ro.properties.emplace_back("os.arch", "aarch64");
    // AXRB's OpenXR runtime, offered by the runtime broker (ContentResolver).
    if (!find_beside_exe(L"guest\\lib64\\libopenxr_runtime.so").empty()) {
        ro.properties.emplace_back("axrb.openxr.runtime", "/apex/com.axrb.direct/lib64/libopenxr_runtime.so");
    }
    ro.properties.emplace_back("os.version", "5.10.226-android12-9-axrb");
    ro.properties.emplace_back("http.agent", "Dalvik/2.1.0 (Linux; U; Android 16; sdk_gphone64_arm64 Build/BP22.250325.006)");
    ro.exit_hook = [](int status) {
        LOGI("app", "System.exit(%d)", status);
        log_flush();
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(status & 0xff));
    };
    ro.fatal_hook = [](std::string_view description) {
        LOGE("app", "the application crashed: %.*s", static_cast<int>(description.size()), description.data());
        log_flush();
        TerminateProcess(GetCurrentProcess(), 10);
    };
    ro.spawn_thread = spawn_java_thread;
    jni::configure(ro);

    auto runtime = vm::Runtime::Create(std::move(ro));
    if (!runtime.ok()) fatal("managed runtime: %s", runtime.status().ToString().c_str());
    g_runtime = std::move(*runtime);
    vm::Runtime* rt = g_runtime.get();
    start_dump_thread();
    vm::Thread* self = rt->main_thread();

    arun::fw::HostConfig hc;
    hc.headless = true;
    hc.width_px = s.options.width;
    hc.height_px = s.options.height;
    hc.density_dpi = s.options.density_dpi;
    hc.guest_pid = process().pid;
    hc.apk = s.apk;
    auto framework_res = arun::pkg::ZipReader::Open(s.boot_dir + L"\\arun-framework-res.apk");
    if (!framework_res.ok()) fatal("framework resources: %s", framework_res.status().ToString().c_str());
    hc.framework_res = std::make_shared<arun::pkg::ZipReader>(std::move(*framework_res));
    auto host = arun::fw::FrameworkHost::Create(std::move(hc));
    if (!host.ok()) fatal("framework host: %s", host.status().ToString().c_str());
    g_framework = std::move(*host);
    g_framework->RegisterNatives(rt);
    register_direct_natives(rt, s.apk, s.guest_apk);
    rt->RegisterNative("Landroid/app/ActivityThread;.nativeWaitForStopRequest()V", ActivityThread_nativeWaitForStopRequest);
    NdkConfig ndk;
    ndk.apk = s.apk;
    ndk.guest_apk = s.guest_apk;
    ndk.width = s.options.width;
    ndk.height = s.options.height;
    ndk.density_dpi = s.options.density_dpi;
    ndk_init(ndk);
    audio_init();
    vulkan_init();
    g_framework->BindUiThread();
    jni::install(rt);

    vm::RootScope roots(self);
    // The application's class loader: a PathClassLoader over the APK.
    vm::RtClass* pcl = rt->class_linker().FindClass(self, "Ldalvik/system/PathClassLoader;", nullptr);
    if (!pcl || !rt->class_linker().EnsureInitialized(self, pcl)) fail_with_exception(rt, self, "PathClassLoader");
    vm::JValue result{};
    vm::RtMethod* get_system = rt->FindMethod(rt->wk.class_loader, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    if (!get_system || !rt->Invoke(self, get_system, nullptr, {}, &result)) fail_with_exception(rt, self, "getSystemClassLoader");
    auto parent = roots.Add(result.l);
    auto loader = roots.Add(rt->heap().AllocObject(self, pcl));
    auto apk_path = roots.Add(rt->NewStringUtf8(self, s.guest_apk));
    auto lib_path = roots.Add(rt->NewStringUtf8(self, s.lib_dir));
    vm::RtMethod* ctor =
        rt->FindMethod(pcl, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    vm::JValue ctor_args[3];
    ctor_args[0].l = apk_path.Get();
    ctor_args[1].l = lib_path.Get();
    ctor_args[2].l = parent.Get();
    if (!ctor || !rt->Invoke(self, ctor, loader.Get(), ctor_args, &result)) fail_with_exception(rt, self, "opening the APK's DEX files");
    g_framework->SetAppClassLoader(loader.Get());
    jni::set_app_loader(vm::natives::LoaderFromJava(rt, loader.Get()));
    if (vm::RtMethod* set_ccl = rt->FindMethod(rt->wk.thread, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V")) {
        vm::JValue arg;
        arg.l = loader.Get();
        if (!rt->Invoke(self, set_ccl, self->peer, std::span<const vm::JValue>(&arg, 1), &result))
            fail_with_exception(rt, self, "setContextClassLoader");
    }

    const auto& m = s.manifest;
    std::string label = m.package;
    if (m.application_label.literal) label = *m.application_label.literal;
    const int target_sdk = m.target_sdk ? m.target_sdk : m.min_sdk;
    const std::vector<std::string> args = {m.package,          m.application_class, s.activity,
                                           label,              s.guest_apk,         s.data_dir,
                                           std::to_string(target_sdk), std::to_string(m.min_sdk)};
    auto array = roots.Add(rt->heap().AllocArray(self, rt->wk.string_array, static_cast<int32_t>(args.size())));
    for (size_t i = 0; i < args.size(); ++i)
        static_cast<vm::Array*>(array.Get())->elements<vm::Object*>()[i] = rt->NewStringUtf8(self, args[i]);
    vm::RtClass* at = rt->class_linker().FindClass(self, "Landroid/app/ActivityThread;", nullptr);
    vm::RtMethod* main = at ? rt->FindMethod(at, "main", "([Ljava/lang/String;)V") : nullptr;
    if (!main) fail_with_exception(rt, self, "ActivityThread.main");
    LOGI("app", "starting %s/%s", m.package.c_str(), s.activity.c_str());
    vm::JValue main_arg;
    main_arg.l = array.Get();
    if (!rt->Invoke(self, main, nullptr, std::span<const vm::JValue>(&main_arg, 1), &result)) rt->HandleUncaughtException(self);
    LOGI("app", "main thread finished");
    (void)main_thread;
    return 0;
}

}  // namespace

bool session_stop_requested() {
    HANDLE event = stop_event();
    return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

void request_session_end(const char* why) {
    static std::atomic<bool> requested{false};
    if (HANDLE event = stop_event()) SetEvent(event);
    if (requested.exchange(true)) return;
    LOGI("app", "ending the session (%s)", why);
    // The activity finishes on its main thread and the process exits when
    // ActivityThread.main returns; a game that hangs on the way is cut off.
    std::thread([] {
        Sleep(20000);
        LOGW("app", "the application did not finish within 20 s; terminating");
        log_flush();
        TerminateProcess(GetCurrentProcess(), 0);
    }).detach();
}

bool app_prepare(const AppOptions& options, std::string& guest_program, std::vector<std::string>& env) {
    Session& s = g_session;
    s.options = options;
    auto zip = arun::pkg::ZipReader::Open(options.apk);
    if (!zip.ok()) {
        LOGE("app", "cannot open %s: %s", narrow(options.apk).c_str(), zip.status().ToString().c_str());
        return false;
    }
    s.apk = std::make_shared<arun::pkg::ZipReader>(std::move(*zip));
    const auto* manifest_entry = s.apk->Find("AndroidManifest.xml");
    if (!manifest_entry) {
        LOGE("app", "%s has no AndroidManifest.xml", narrow(options.apk).c_str());
        return false;
    }
    auto manifest_bytes = s.apk->Extract(*manifest_entry);
    if (!manifest_bytes.ok()) {
        LOGE("app", "manifest: %s", manifest_bytes.status().ToString().c_str());
        return false;
    }
    auto manifest = arun::manifest::Parse(*manifest_bytes);
    if (!manifest.ok()) {
        LOGE("app", "manifest: %s", manifest.status().ToString().c_str());
        return false;
    }
    s.manifest = std::move(*manifest);
    s.package = s.manifest.package;
    if (!options.activity.empty()) {
        s.activity = arun::manifest::QualifyClassName(s.package, options.activity);
    } else {
        auto launcher = s.manifest.LauncherActivity();
        if (!launcher.ok()) {
            LOGE("app", "no launcher activity: %s", launcher.status().ToString().c_str());
            return false;
        }
        s.activity = *launcher;
    }

    s.boot_dir = find_beside_exe(L"vm\\boot");
    if (s.boot_dir.empty()) s.boot_dir = find_beside_exe(L"boot");
    std::wstring guest_bin = find_beside_exe(L"guest\\axrb_app");
    if (s.boot_dir.empty() || guest_bin.empty()) {
        LOGE("app", "AXRB Direct is incomplete: boot class path or guest launcher missing next to the executable");
        return false;
    }
    guest_bin = guest_bin.substr(0, guest_bin.find_last_of(L'\\'));

    // Missing proxies otherwise fall back to AOSP libraries: Unity then waits
    // forever for a native window from services that Direct mode does not run.
    for (const wchar_t* name : {L"libandroid.so", L"libvulkan.so", L"libEGL.so", L"libGLESv1_CM.so",
                              L"libGLESv2.so", L"libGLESv3.so", L"libaaudio.so", L"libOpenSLES.so",
                              L"libopenxr_runtime.so"}) {
        const auto file = guest_bin + L"\\lib64\\" + name;
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error) || std::filesystem::file_size(file, error) == 0 || error) {
            LOGE("app", "Direct mode installation is incomplete: missing or empty %s. Reinstall the AXRB launcher.",
                 narrow(file).c_str());
            return false;
        }
    }

    // Install layout.
    s.install_dir = "/data/app/~~axrb==/" + s.package + "-1";
    s.guest_apk = s.install_dir + "/base.apk";
    s.lib_dir = s.install_dir + "/lib/arm64";
    s.data_dir = "/data/data/" + s.package;
    std::wstring app_root = options.data_root + L"\\apps\\" + widen(s.package);
    std::wstring lib_host = app_root + L"\\lib\\arm64";
    if (!extract_libraries(*s.apk, lib_host)) return false;
    vfs().add_mount(s.guest_apk, options.apk, true, true);
    vfs().add_mount(s.lib_dir, lib_host, true);
    vfs().add_mount("/apex/com.axrb.direct/bin", guest_bin, true);
    // The NDK libraries AXRB provides in place of the system ones (libandroid,
    // libvulkan, libEGL, audio): first on the library path.
    vfs().add_mount("/apex/com.axrb.direct/lib64", guest_bin + L"\\lib64", true);

    std::wstring data_host = options.data_root + L"\\data\\data\\" + widen(s.package);
    for (const wchar_t* sub : {L"files", L"cache", L"code_cache", L"shared_prefs", L"databases"}) ensure_dir(data_host + L"\\" + sub);
    vfs().add_symlink("/data/user/0", "/data/data");
    std::wstring external = options.data_root + L"\\sdcard\\Android\\data\\" + widen(s.package);
    ensure_dir(external + L"\\files");
    ensure_dir(external + L"\\cache");
    if (!options.obb_dir.empty()) {
        vfs().add_mount("/storage/emulated/0/Android/obb/" + s.package, options.obb_dir, true);
        LOGI("app", "obb: %s", narrow(options.obb_dir).c_str());
    } else {
        ensure_dir(options.data_root + L"\\sdcard\\Android\\obb\\" + widen(s.package));
    }
    // Content files used where the launcher keeps them: one "guest<TAB>host"
    // line per file, guest relative to shared storage (Android/obb/<package>/...).
    if (!options.asset_list.empty()) {
        std::vector<uint8_t> list = read_host_file(options.asset_list);
        std::string text(list.begin(), list.end());
        const std::string own_obb = "Android/obb/" + s.package + "/", own_data = "Android/data/" + s.package + "/";
        size_t mounted = 0;
        for (size_t at = 0; at < text.size();) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string guest = line.substr(0, tab);
            if ((guest.rfind(own_obb, 0) != 0 && guest.rfind(own_data, 0) != 0) || guest.find("..") != std::string::npos) {
                LOGW("app", "ignoring content file outside the game's storage: %s", guest.c_str());
                continue;
            }
            vfs().add_mount("/storage/emulated/0/" + guest, widen(line.substr(tab + 1)), true, true);
            ++mounted;
        }
        LOGI("app", "%zu content files from %s", mounted, narrow(options.asset_list).c_str());
    }

    register_named_function("axrb.app_main", [](HostCall& c) {
        GuestApi api{};
        guest_read(c.x(0), &api, sizeof api);
        if (api.version != 1) fatal("guest launcher API version %u", api.version);
        guest_api() = api;
        c.ret(static_cast<uint64_t>(run_app(c.thread())));
    });
    register_named_function("axrb.thread_main", [](HostCall& c) {
        auto* start = reinterpret_cast<ThreadStart*>(c.x(0));
        ThreadStart copy = *start;
        delete start;
        SetEvent(copy.started);
        CloseHandle(copy.started);
        copy.entry(copy.arg);
    });

    guest_program = "/apex/com.axrb.direct/bin/axrb_app";
    env.push_back("LD_LIBRARY_PATH=/apex/com.axrb.direct/lib64:" + s.lib_dir);
    env.push_back("ANDROID_APP_PACKAGE=" + s.package);
    process().package = s.package;
    process().uid = process().gid = 10123;
    LOGI("app", "%s (%s), activity %s", s.package.c_str(), narrow(options.apk).c_str(), s.activity.c_str());
    return true;
}

}  // namespace axrb
