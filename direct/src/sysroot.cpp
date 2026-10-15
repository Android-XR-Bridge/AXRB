#include "sysroot.h"
#include "binder.h"

#include "files.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "vfs.h"

#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <shlobj.h>

#include <cstring>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace axrb {
using namespace linux_abi;

int guest_cpu_count();

namespace {

std::wstring exe_directory() {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring dir(path, n);
    size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

bool is_directory(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// The few JSON files mkroot.py writes are flat {"a": "b", ...} maps.
std::vector<std::pair<std::string, std::string>> read_flat_json(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::pair<std::string, std::string>> out;
    std::vector<std::string> strings;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') continue;
        std::string s;
        for (++i; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            s.push_back(text[i]);
        }
        strings.push_back(s);
    }
    for (size_t i = 0; i + 1 < strings.size(); i += 2) out.push_back({strings[i], strings[i + 1]});
    return out;
}

std::string format(const char* fmt, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return std::string(buffer, static_cast<size_t>(std::max(0, std::min<int>(n, sizeof buffer - 1))));
}

uint64_t total_ram() {
    MEMORYSTATUSEX status{sizeof status};
    GlobalMemoryStatusEx(&status);
    return std::min<uint64_t>(status.ullTotalPhys, 8ull << 30);
}

uint64_t available_ram() {
    MEMORYSTATUSEX status{sizeof status};
    GlobalMemoryStatusEx(&status);
    return std::min<uint64_t>(status.ullAvailPhys, total_ram() * 3 / 4);
}

std::string cpuinfo() {
    std::string text;
    for (int i = 0; i < guest_cpu_count(); ++i) {
        text += format(
            "processor\t: %d\nBogoMIPS\t: 38.40\nFeatures\t: fp asimd evtstrm aes pmull sha1 sha2 crc32\n"
            "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0xd4b\nCPU revision\t: 0\n\n",
            i);
    }
    text += "Hardware\t: ranchu\n";
    return text;
}

std::string meminfo() {
    uint64_t total = total_ram() / 1024, available = available_ram() / 1024;
    return format(
        "MemTotal:       %8llu kB\nMemFree:        %8llu kB\nMemAvailable:   %8llu kB\nBuffers:               0 kB\n"
        "Cached:         %8llu kB\nSwapCached:            0 kB\nActive:         %8llu kB\nInactive:       %8llu kB\n"
        "SwapTotal:             0 kB\nSwapFree:              0 kB\nDirty:                 0 kB\n"
        "AnonPages:      %8llu kB\nMapped:         %8llu kB\nShmem:                 0 kB\n"
        "CommitLimit:    %8llu kB\nCommitted_AS:   %8llu kB\nVmallocTotal:   263061440 kB\n",
        (unsigned long long)total, (unsigned long long)(available / 2), (unsigned long long)available,
        (unsigned long long)(available / 2), (unsigned long long)(total - available) / 2,
        (unsigned long long)(total - available) / 2, (unsigned long long)(total - available) / 2,
        (unsigned long long)(total - available) / 4, (unsigned long long)total,
        (unsigned long long)(address_space().mapped_bytes() / 1024 / 8));
}

std::string proc_stat() {
    // Jiffies from host idle/kernel/user times so CPU-load heuristics see
    // plausible numbers.
    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);
    auto ticks = [](const FILETIME& f) {
        return ((static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 100000;  // 100 Hz
    };
    uint64_t i = ticks(idle), k = ticks(kernel) - i, u = ticks(user);
    int cpus = guest_cpu_count();
    std::string text = format("cpu  %llu 0 %llu %llu 0 0 0 0 0 0\n", (unsigned long long)u, (unsigned long long)k,
                              (unsigned long long)i);
    for (int c = 0; c < cpus; ++c)
        text += format("cpu%d %llu 0 %llu %llu 0 0 0 0 0 0\n", c, (unsigned long long)(u / cpus),
                       (unsigned long long)(k / cpus), (unsigned long long)(i / cpus));
    text += format("intr 0\nctxt 0\nbtime %lld\nprocesses 1\nprocs_running 1\nprocs_blocked 0\n",
                   (long long)((realtime_ns() - monotonic_ns()) / 1000000000));
    return text;
}

std::string thread_stat(int tid, const std::string& name) {
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters);
    uint64_t vsize = address_space().mapped_bytes();
    uint64_t rss_pages = counters.WorkingSetSize / 4096;
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto ticks = [](const FILETIME& f) {
        return ((static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 100000;
    };
    return format(
        "%d (%s) R 1 %d %d 0 -1 4210944 0 0 0 0 %llu %llu 0 0 20 0 %zu 0 0 %llu %llu 18446744073709551615 0 0 %llu 0 0 "
        "4612 1073775864 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
        tid, name.c_str(), process().pid, process().pid, (unsigned long long)ticks(u), (unsigned long long)ticks(k),
        process().threads.size(), (unsigned long long)vsize, (unsigned long long)rss_pages, (unsigned long long)process().start_stack);
}

std::string process_status(int tid, const std::string& name) {
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters);
    uint64_t vm = address_space().mapped_bytes() / 1024;
    return format(
        "Name:\t%s\nUmask:\t0077\nState:\tR (running)\nTgid:\t%d\nNgid:\t0\nPid:\t%d\nPPid:\t1\nTracerPid:\t0\n"
        "Uid:\t%u\t%u\t%u\t%u\nGid:\t%u\t%u\t%u\t%u\nFDSize:\t256\nGroups:\t%u\nVmPeak:\t%8llu kB\nVmSize:\t%8llu kB\n"
        "VmLck:\t       0 kB\nVmPin:\t       0 kB\nVmHWM:\t%8llu kB\nVmRSS:\t%8llu kB\nRssAnon:\t%8llu kB\n"
        "RssFile:\t       0 kB\nRssShmem:\t       0 kB\nVmData:\t%8llu kB\nVmStk:\t    8192 kB\nVmExe:\t       0 kB\n"
        "VmLib:\t       0 kB\nVmSwap:\t       0 kB\nThreads:\t%zu\nSigQ:\t0/31529\nSigPnd:\t0000000000000000\n"
        "ShdPnd:\t0000000000000000\nSigBlk:\t0000000000000000\nSigIgn:\t0000000000001000\nSigCgt:\t0000000000000000\n"
        "CapInh:\t0000000000000000\nCapPrm:\t0000000000000000\nCapEff:\t0000000000000000\nCapBnd:\t0000000000000000\n"
        "NoNewPrivs:\t0\nSeccomp:\t0\nCpus_allowed_list:\t0-%d\nvoluntary_ctxt_switches:\t0\n"
        "nonvoluntary_ctxt_switches:\t0\n",
        name.c_str(), process().pid, tid, process().uid, process().uid, process().uid, process().uid, process().gid,
        process().gid, process().gid, process().gid, process().gid, (unsigned long long)vm, (unsigned long long)vm,
        (unsigned long long)(counters.PeakWorkingSetSize / 1024), (unsigned long long)(counters.WorkingSetSize / 1024),
        (unsigned long long)(counters.WorkingSetSize / 1024), (unsigned long long)vm, process().threads.size(),
        guest_cpu_count() - 1);
}

std::string random_uuid() {
    uint8_t b[16];
    BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    return format("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", b[0], b[1], b[2], b[3], b[4],
                  b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

std::string thread_name(int tid) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    auto it = process().threads.find(tid);
    return it == process().threads.end() ? std::string() : it->second->name;
}

bool thread_exists(int tid) {
    std::lock_guard<std::mutex> held(process().threads_lock);
    return process().threads.count(tid) != 0;
}

std::vector<std::string> thread_ids() {
    std::lock_guard<std::mutex> held(process().threads_lock);
    std::vector<std::string> out;
    for (auto& [tid, thread] : process().threads) out.push_back(std::to_string(tid));
    return out;
}

void make_file(Resolved& out, std::function<std::string()> reader) {
    out.kind = Resolved::Synthetic;
    out.reader = std::move(reader);
    out.read_only = true;
}

void make_dir(Resolved& out, std::vector<std::string> children) {
    out.kind = Resolved::SyntheticDir;
    out.children = std::move(children);
    out.read_only = true;
}

void make_link(Resolved& out, std::string target) {
    out.kind = Resolved::None;
    out.guest = std::move(target);
}

std::string fd_target(int fd) {
    auto file = fds().get(fd);
    if (!file) return {};
    if (!file->guest_path().empty()) return file->guest_path();
    kstat st{};
    file->stat(st);
    const char* kind = (st.st_mode & S_IFMT_) == S_IFSOCK_ ? "socket" : (st.st_mode & S_IFMT_) == S_IFIFO_ ? "pipe" : "anon_inode";
    if (std::strcmp(kind, "anon_inode") == 0) return "anon_inode:[eventfd]";
    return format("%s:[%llu]", kind, (unsigned long long)file->inode());
}

// Everything below /proc/<pid> (and /proc/self/task/<tid>).
bool resolve_task(const std::string& rest, int tid, bool process_level, Resolved& out) {
    std::string name = thread_name(tid);
    if (name.empty()) name = "main";
    if (rest.empty()) {
        std::vector<std::string> children = {"stat", "status", "comm", "cmdline", "maps", "smaps", "statm", "exe",
                                             "fd", "cwd", "auxv", "environ", "mounts", "mountinfo", "limits", "cgroup",
                                             "oom_score_adj", "oom_adj", "timerslack_ns", "sched", "schedstat", "io"};
        if (process_level) children.push_back("task");
        make_dir(out, children);
        return true;
    }
    if (rest == "/stat") return make_file(out, [=] { return thread_stat(tid, name); }), true;
    if (rest == "/status") return make_file(out, [=] { return process_status(tid, name); }), true;
    if (rest == "/comm") return make_file(out, [=] { return name + "\n"; }), true;
    if (rest == "/cmdline") return make_file(out, [] { return process().cmdline; }), true;
    if (rest == "/maps" || rest == "/smaps") return make_file(out, [] { return address_space().maps_text(); }), true;
    if (rest == "/statm") {
        return make_file(out, [] {
            uint64_t pages = address_space().mapped_bytes() / 4096;
            return format("%llu %llu 0 0 0 %llu 0\n", (unsigned long long)pages, (unsigned long long)(pages / 4),
                          (unsigned long long)(pages / 2));
        }), true;
    }
    if (rest == "/exe") return make_link(out, process().exe_path), true;
    if (rest == "/cwd") return make_link(out, process().cwd), true;
    if (rest == "/auxv") {
        return make_file(out, [] {
            const auto& v = process().auxv;
            return std::string(reinterpret_cast<const char*>(v.data()), v.size() * 8);
        }), true;
    }
    if (rest == "/environ") return make_file(out, [] { return std::string(); }), true;
    if (rest == "/mounts" || rest == "/mountinfo") {
        return make_file(out, [] {
            return std::string(
                "/dev/root / ext4 ro,seclabel,relatime 0 0\n"
                "tmpfs /dev tmpfs rw,seclabel,nosuid,relatime,mode=755 0 0\n"
                "proc /proc proc rw,relatime,gid=3009,hidepid=invisible 0 0\n"
                "sysfs /sys sysfs rw,seclabel,relatime 0 0\n"
                "/dev/block/dm-5 /data f2fs rw,lazytime,seclabel,nosuid,nodev,noatime 0 0\n"
                "/dev/fuse /storage/emulated fuse rw,lazytime,nosuid,nodev,noexec,noatime 0 0\n");
        }), true;
    }
    if (rest == "/limits") {
        return make_file(out, [] {
            return std::string(
                "Limit                     Soft Limit           Hard Limit           Units     \n"
                "Max stack size            8388608              unlimited            bytes     \n"
                "Max open files            32768                32768                files     \n");
        }), true;
    }
    if (rest == "/cgroup") return make_file(out, [] { return std::string("0::/uid_10123/pid_1\n"); }), true;
    if (rest == "/oom_score_adj") return make_file(out, [] { return std::string("0\n"); }), true;
    if (rest == "/oom_adj") return make_file(out, [] { return std::string("0\n"); }), true;
    if (rest == "/timerslack_ns") return make_file(out, [] { return std::string("50000\n"); }), true;
    if (rest == "/sched" || rest == "/schedstat" || rest == "/io") return make_file(out, [] { return std::string(); }), true;
    if (rest == "/fd" || rest == "/fdinfo") {
        std::vector<std::string> children;
        for (int fd : fds().list()) children.push_back(std::to_string(fd));
        make_dir(out, children);
        return true;
    }
    if (rest.rfind("/fd/", 0) == 0) {
        int fd = std::atoi(rest.c_str() + 4);
        std::string target = fd_target(fd);
        if (target.empty()) return false;
        make_link(out, target);
        return true;
    }
    if (rest.rfind("/fdinfo/", 0) == 0) {
        int fd = std::atoi(rest.c_str() + 8);
        auto file = fds().get(fd);
        if (!file) return false;
        uint32_t flags = file->status_flags;
        return make_file(out, [=] { return format("pos:\t0\nflags:\t0%o\nmnt_id:\t1\n", flags); }), true;
    }
    if (process_level && rest == "/task") return make_dir(out, thread_ids()), true;
    if (process_level && rest.rfind("/task/", 0) == 0) {
        std::string tail = rest.substr(6);
        size_t slash = tail.find('/');
        int child = std::atoi(tail.substr(0, slash).c_str());
        if (!thread_exists(child)) return false;
        return resolve_task(slash == std::string::npos ? "" : tail.substr(slash), child, false, out);
    }
    return false;
}

bool resolve_proc(const std::string& rest, Resolved& out) {
    const std::string pid = std::to_string(process().pid);
    if (rest.empty()) {
        make_dir(out, {"self", "thread-self", pid, "cpuinfo", "meminfo", "stat", "uptime", "version", "loadavg",
                       "filesystems", "sys"});
        return true;
    }
    if (rest == "/self") return make_link(out, "/proc/" + pid), true;
    if (rest == "/thread-self") {
        GuestThread* t = current_thread();
        return make_link(out, "/proc/" + pid + "/task/" + std::to_string(t ? t->tid : process().pid)), true;
    }
    if (rest == "/" + pid || rest.rfind("/" + pid + "/", 0) == 0)
        return resolve_task(rest.substr(pid.size() + 1), process().pid, true, out);
    if (rest == "/cpuinfo") return make_file(out, cpuinfo), true;
    if (rest == "/meminfo") return make_file(out, meminfo), true;
    if (rest == "/stat") return make_file(out, proc_stat), true;
    if (rest == "/uptime") {
        return make_file(out, [] {
            double up = monotonic_ns() / 1e9;
            return format("%.2f %.2f\n", up, up * 0.5);
        }), true;
    }
    if (rest == "/loadavg") return make_file(out, [] { return std::string("0.50 0.40 0.30 1/400 1\n"); }), true;
    if (rest == "/version")
        return make_file(out, [] { return std::string("Linux version 5.10.226-android12-9-axrb (axrb@direct) #1 SMP PREEMPT\n"); }), true;
    if (rest == "/filesystems") return make_file(out, [] { return std::string("nodev\tproc\nnodev\ttmpfs\n\text4\n\tf2fs\nnodev\tfuse\n"); }), true;
    if (rest == "/sys/kernel/random/boot_id") {
        static std::string boot_id = random_uuid();
        return make_file(out, [] { return boot_id; }), true;
    }
    if (rest == "/sys/kernel/random/uuid") return make_file(out, random_uuid), true;
    if (rest == "/sys/kernel/random/entropy_avail") return make_file(out, [] { return std::string("256\n"); }), true;
    if (rest == "/sys/kernel/pid_max") return make_file(out, [] { return std::string("32768\n"); }), true;
    if (rest == "/sys/kernel/osrelease") return make_file(out, [] { return std::string("5.10.226-android12-9-axrb\n"); }), true;
    if (rest == "/sys/kernel/perf_event_paranoid") return make_file(out, [] { return std::string("3\n"); }), true;
    if (rest == "/sys/vm/overcommit_memory") return make_file(out, [] { return std::string("1\n"); }), true;
    if (rest == "/sys/vm/max_map_count") return make_file(out, [] { return std::string("65530\n"); }), true;
    if (rest == "/sys/fs/inotify/max_user_watches") return make_file(out, [] { return std::string("8192\n"); }), true;
    if (rest == "/sys" || rest == "/sys/kernel" || rest == "/sys/vm" || rest == "/sys/kernel/random")
        return make_dir(out, {}), true;
    return false;
}

// /sys/devices/system/cpu: what engines read to find core counts and the
// "big" cores. A uniform cluster at Quest-3-like clocks.
bool resolve_cpu(const std::string& rest, Resolved& out) {
    int cpus = guest_cpu_count();
    std::string range = cpus > 1 ? format("0-%d\n", cpus - 1) : std::string("0\n");
    if (rest.empty()) {
        std::vector<std::string> children = {"possible", "present", "online", "offline", "kernel_max"};
        for (int i = 0; i < cpus; ++i) children.push_back("cpu" + std::to_string(i));
        make_dir(out, children);
        return true;
    }
    if (rest == "/possible" || rest == "/present" || rest == "/online") return make_file(out, [=] { return range; }), true;
    if (rest == "/offline") return make_file(out, [] { return std::string("\n"); }), true;
    if (rest == "/kernel_max") return make_file(out, [] { return std::string("31\n"); }), true;
    if (rest.rfind("/cpu", 0) != 0) return false;
    size_t end = 4;
    while (end < rest.size() && std::isdigit(static_cast<unsigned char>(rest[end]))) ++end;
    if (end == 4) return false;
    int index = std::atoi(rest.substr(4, end - 4).c_str());
    if (index >= cpus) return false;
    std::string tail = rest.substr(end);
    if (tail.empty()) return make_dir(out, {"online", "cpufreq", "topology", "cpu_capacity"}), true;
    if (tail == "/online") return make_file(out, [] { return std::string("1\n"); }), true;
    if (tail == "/cpu_capacity") return make_file(out, [] { return std::string("1024\n"); }), true;
    if (tail == "/cpufreq")
        return make_dir(out, {"cpuinfo_max_freq", "cpuinfo_min_freq", "scaling_max_freq", "scaling_min_freq",
                              "scaling_cur_freq", "cpuinfo_cur_freq", "related_cpus", "affected_cpus",
                              "scaling_governor", "scaling_available_frequencies"}),
               true;
    if (tail == "/cpufreq/cpuinfo_max_freq" || tail == "/cpufreq/scaling_max_freq" ||
        tail == "/cpufreq/scaling_cur_freq" || tail == "/cpufreq/cpuinfo_cur_freq")
        return make_file(out, [] { return std::string("2841600\n"); }), true;
    if (tail == "/cpufreq/cpuinfo_min_freq" || tail == "/cpufreq/scaling_min_freq")
        return make_file(out, [] { return std::string("300000\n"); }), true;
    if (tail == "/cpufreq/scaling_available_frequencies")
        return make_file(out, [] { return std::string("300000 1497600 2841600\n"); }), true;
    if (tail == "/cpufreq/related_cpus" || tail == "/cpufreq/affected_cpus") {
        std::string list;
        for (int i = 0; i < cpus; ++i) list += (i ? " " : "") + std::to_string(i);
        return make_file(out, [=] { return list + "\n"; }), true;
    }
    if (tail == "/cpufreq/scaling_governor") return make_file(out, [] { return std::string("schedutil\n"); }), true;
    if (tail == "/topology") return make_dir(out, {"core_id", "physical_package_id", "cluster_id", "core_siblings_list"}), true;
    if (tail == "/topology/core_id") return make_file(out, [=] { return std::to_string(index) + "\n"; }), true;
    if (tail == "/topology/physical_package_id" || tail == "/topology/cluster_id")
        return make_file(out, [] { return std::string("0\n"); }), true;
    if (tail == "/topology/core_siblings_list") return make_file(out, [=] { return range; }), true;
    return false;
}

}  // namespace

std::wstring default_sysroot() {
    wchar_t env[MAX_PATH * 4];
    if (DWORD n = GetEnvironmentVariableW(L"AXRB_DIRECT_SYSROOT", env, static_cast<DWORD>(std::size(env))))
        return std::wstring(env, n);
    std::wstring dir = exe_directory();
    if (is_directory(dir + L"\\sysroot\\system")) return dir + L"\\sysroot";
    // Development layout: walk up to the repository's out\sysimg\root64.
    for (std::wstring at = dir; !at.empty();) {
        if (is_directory(at + L"\\out\\sysimg\\root64\\system")) return at + L"\\out\\sysimg\\root64";
        size_t slash = at.find_last_of(L"\\/");
        if (slash == std::wstring::npos || slash < 3) break;
        at = at.substr(0, slash);
    }
    return dir + L"\\sysroot";
}

std::wstring data_root() {
    wchar_t env[MAX_PATH * 4];
    if (DWORD n = GetEnvironmentVariableW(L"AXRB_DIRECT_DATA", env, static_cast<DWORD>(std::size(env))))
        return std::wstring(env, n);
    PWSTR local = nullptr;
    std::wstring root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) root = local;
    CoTaskMemFree(local);
    if (root.empty()) root = exe_directory();
    return root + L"\\AXRB Runtime\\direct";
}

bool sysroot_init(const std::wstring& root) {
    if (!is_directory(root + L"\\system\\lib64")) {
        LOGE("sysroot", "no Android system root at %s (run out/sysimg/mkroot.py)", narrow(root).c_str());
        return false;
    }
    vfs().add_mount("/", root, true);
    int links = 0;
    for (auto& [from, to] : read_flat_json(root + L"\\.symlinks.json")) {
        vfs().add_symlink(from, to);
        ++links;
    }
    // Directories bionic, the linker and libraries expect.
    vfs().add_symlink("/bin", "/system/bin");
    vfs().add_symlink("/etc", "/system/etc");
    vfs().add_symlink("/sdcard", "/storage/self/primary");
    vfs().add_symlink("/storage/self/primary", "/storage/emulated/0");
    // Android normally generates this at boot. Direct mode has no init or
    // linkerconfig service, so supply the search paths for our extracted
    // system libraries. In particular harfbuzz (loaded by Unity) needs ICU
    // from the i18n APEX. LD_LIBRARY_PATH still takes precedence for the
    // app's libraries and our NDK proxies.
    vfs().add_synthetic_dir("/linkerconfig", {"ld.config.txt"});
    const bool has_art = is_directory(root + L"\\apex\\com.android.art");
    std::vector<std::pair<std::string, std::string>> apex_namespaces;
    if (has_art) {
        for (const auto& item : std::filesystem::directory_iterator(root + L"\\apex")) {
            if (!item.is_directory()) continue;
            std::string name = item.path().filename().string(), ns = name;
            std::replace(ns.begin(), ns.end(), '.', '_');
            apex_namespaces.emplace_back(name, ns);
        }
        std::sort(apex_namespaces.begin(), apex_namespaces.end());
    }
    vfs().add_synthetic("/linkerconfig/ld.config.txt", [has_art, apex_namespaces] {
        std::string config(
            "dir.direct = /apex\n"
            "dir.direct = /data\n"
            "dir.direct = /system\n"
            "[direct]\n"
            "namespace.default.isolated = false\n"
            "namespace.default.visible = true\n"
            "namespace.default.search.paths = /system/${LIB}:/apex/com.android.runtime/${LIB}/bionic:/apex/com.android.i18n/${LIB}\n");
        if (has_art) {
            config += "namespace.default.search.paths += ";
            for (size_t i = 0; i < apex_namespaces.size(); ++i)
                config += (i ? ":" : "") + std::string("/apex/") + apex_namespaces[i].first + "/${LIB}";
            config += "\nadditional.namespaces = ";
            for (size_t i = 0; i < apex_namespaces.size(); ++i)
                config += (i ? "," : "") + apex_namespaces[i].second;
            config += "\n";
            // ART resolves boot-JAR JNI libraries using exported APEX names.
            // Delegate to the common namespace so libc/libart are not loaded twice.
            for (const auto& [apex, name] : apex_namespaces) {
                std::string prefix = std::string("namespace.") + name;
                config += prefix + ".isolated = false\n" + prefix + ".visible = true\n";
                config += prefix + ".links = default\n" + prefix + ".link.default.allow_all_shared_libs = true\n";
            }
        }
        return config;
    });
    LOGI("sysroot", "system root %s (%d symlinks)", narrow(root).c_str(), links);
    return true;
}

void proc_init() {
    vfs().set_dynamic_dir("/proc", resolve_proc);
    vfs().set_dynamic_dir("/sys/devices/system/cpu", resolve_cpu);
    vfs().add_synthetic("/sys/kernel/mm/transparent_hugepage/enabled", [] { return std::string("always [madvise] never\n"); });
    vfs().add_synthetic_dir("/sys/fs/cgroup", {});
    vfs().add_device("/dev/null", "null");
    vfs().add_device("/dev/zero", "zero");
    vfs().add_device("/dev/full", "zero");
    vfs().add_device("/dev/random", "random");
    vfs().add_device("/dev/urandom", "urandom");
    vfs().add_device("/dev/tty", "null");
    init_binder();
    vfs().add_synthetic_dir("/dev/socket", {"logdw", "property_service", "dnsproxyd"});
}

}  // namespace axrb
