#include "vfs.h"

#include "linux.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <set>

namespace axrb {
using namespace linux_abi;

Vfs& vfs() {
    static Vfs instance;
    return instance;
}

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// Characters Windows refuses in a name go to U+F000 + c, the convention
// Cygwin and WSL use, so they show up recognisably in Explorer.
std::wstring escape_component(const std::string& name) {
    std::wstring wide = widen(name);
    std::wstring out;
    out.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        wchar_t c = wide[i];
        bool trailing = (i + 1 == wide.size()) && (c == L'.' || c == L' ') && name != "." && name != "..";
        if (c < 0x20 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' ||
            c == L'\\' || trailing)
            out.push_back(static_cast<wchar_t>(0xf000 + c));
        else
            out.push_back(c);
    }
    return out;
}

std::string unescape_component(const std::wstring& name) {
    std::wstring out = name;
    for (auto& c : out)
        if (c >= 0xf000 && c < 0xf080) c = static_cast<wchar_t>(c - 0xf000);
    return narrow(out);
}

static std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at < path.size()) {
        size_t slash = path.find('/', at);
        if (slash == std::string::npos) slash = path.size();
        if (slash > at) parts.push_back(path.substr(at, slash - at));
        at = slash + 1;
    }
    return parts;
}

std::string Vfs::normalize(const std::string& base, const std::string& path) {
    std::string joined = (!path.empty() && path[0] == '/') ? path : (base + "/" + path);
    std::vector<std::string> out;
    for (auto& part : split_path(joined)) {
        if (part == ".") continue;
        if (part == "..") {
            if (!out.empty()) out.pop_back();
            continue;
        }
        out.push_back(part);
    }
    std::string result;
    for (auto& part : out) result += "/" + part;
    return result.empty() ? "/" : result;
}

void Vfs::add_mount(const std::string& guest, const std::wstring& host, bool read_only, bool is_file) {
    // Mount ancestors in writable storage must also exist on the host. A
    // purely virtual Android/obb ancestor otherwise makes File.mkdirs stop
    // early, and creating its Android/data sibling fails with ENOENT.
    const std::string normalized = normalize("/", guest);
    const size_t slash = normalized.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        Resolved parent;
        if (!resolve(normalized.substr(0, slash), true, parent) && parent.mount &&
            !parent.mount->read_only && !parent.host.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent.host, error);
            if (error) LOGW("vfs", "cannot create mount parent for %s: %s", guest.c_str(), error.message().c_str());
        }
    }
    std::unique_lock<std::shared_mutex> held(lock_);
    std::string prefix = guest == "/" ? "" : normalize("/", guest);
    mounts_.erase(std::remove_if(mounts_.begin(), mounts_.end(), [&](const Mount& m) { return m.guest == prefix; }),
                  mounts_.end());
    mounts_.push_back({prefix, host, read_only, is_file});
    std::sort(mounts_.begin(), mounts_.end(), [](const Mount& a, const Mount& b) { return a.guest.size() > b.guest.size(); });
}

void Vfs::add_symlink(const std::string& guest, const std::string& target) {
    std::unique_lock<std::shared_mutex> held(lock_);
    symlinks_[normalize("/", guest)] = target;
}

void Vfs::add_synthetic(const std::string& guest, SyntheticReader reader) {
    std::unique_lock<std::shared_mutex> held(lock_);
    synthetic_[normalize("/", guest)] = std::move(reader);
}

void Vfs::add_synthetic_dir(const std::string& guest, std::vector<std::string> children) {
    std::unique_lock<std::shared_mutex> held(lock_);
    synthetic_dirs_[normalize("/", guest)] = std::move(children);
}

void Vfs::add_device(const std::string& guest, const std::string& device) {
    std::unique_lock<std::shared_mutex> held(lock_);
    devices_[normalize("/", guest)] = device;
}

void Vfs::set_dynamic_dir(const std::string& prefix, std::function<bool(const std::string&, Resolved&)> resolver) {
    std::unique_lock<std::shared_mutex> held(lock_);
    dynamic_.push_back({normalize("/", prefix), std::move(resolver)});
}

std::wstring Vfs::host_path_for(const Mount& mount, const std::string& guest) const {
    std::wstring host = mount.host;
    if (mount.is_file) return host;
    std::string rest = guest.substr(mount.guest.size());
    for (auto& part : split_path(rest)) {
        if (!host.empty() && host.back() != L'\\') host.push_back(L'\\');
        host += escape_component(part);
    }
    return host;
}

static bool host_exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

int Vfs::resolve_once(const std::string& path, Resolved& out) const {
    out = Resolved{};
    out.guest = path;
    if (auto it = devices_.find(path); it != devices_.end()) {
        out.kind = Resolved::Device;
        out.device = it->second;
        return 0;
    }
    if (auto it = synthetic_.find(path); it != synthetic_.end()) {
        out.kind = Resolved::Synthetic;
        out.reader = it->second;
        out.read_only = true;
        return 0;
    }
    if (auto it = synthetic_dirs_.find(path); it != synthetic_dirs_.end()) {
        out.kind = Resolved::SyntheticDir;
        out.children = it->second;
        out.read_only = true;
        return 0;
    }
    for (const auto& [prefix, resolver] : dynamic_) {
        if (path == prefix || (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
                               path[prefix.size()] == '/')) {
            std::string rest = path.substr(prefix.size());
            if (resolver(rest, out)) {
                out.guest = path;
                return out.kind == Resolved::None ? -ENOENT_ : 0;
            }
        }
    }
    for (const Mount& mount : mounts_) {
        bool match = mount.guest.empty() || path == mount.guest ||
                     (path.size() > mount.guest.size() && path.compare(0, mount.guest.size(), mount.guest) == 0 &&
                      path[mount.guest.size()] == '/');
        if (!match) continue;
        if (mount.is_file && path != mount.guest) return -ENOTDIR_;
        out.kind = Resolved::Host;
        out.mount = &mount;
        out.read_only = mount.read_only;
        out.host = host_path_for(mount, path);
        if (!host_exists(out.host)) {
            // A directory that exists only because something is mounted or
            // synthesized beneath it (/data, /proc, /sys/devices...).
            std::string below = path == "/" ? "/" : path + "/";
            bool parent_of_something = false;
            for (const Mount& other : mounts_)
                if (!other.guest.empty() && other.guest.compare(0, below.size(), below) == 0) parent_of_something = true;
            for (const auto& [name, reader] : synthetic_)
                if (name.compare(0, below.size(), below) == 0) parent_of_something = true;
            for (const auto& [name, children] : synthetic_dirs_)
                if (name.compare(0, below.size(), below) == 0) parent_of_something = true;
            for (const auto& [name, target] : symlinks_)
                if (name.compare(0, below.size(), below) == 0) parent_of_something = true;
            for (const auto& [name, device] : devices_)
                if (name.compare(0, below.size(), below) == 0) parent_of_something = true;
            for (const auto& [name, resolver] : dynamic_)
                if (name.compare(0, below.size(), below) == 0) parent_of_something = true;
            if (parent_of_something) {
                out.kind = Resolved::SyntheticDir;
                out.read_only = true;
            }
        }
        return 0;
    }
    return -ENOENT_;
}

bool Vfs::dynamic_link(const std::string& path, std::string& target) const {
    for (const auto& [prefix, resolver] : dynamic_) {
        if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/') {
            Resolved r;
            if (resolver(path.substr(prefix.size()), r) && r.kind == Resolved::None && !r.guest.empty()) {
                target = r.guest;  // dynamic symlink: resolver reports the target in `guest`
                return true;
            }
        }
    }
    return false;
}

int Vfs::resolve(const std::string& absolute, bool follow, Resolved& out) const {
    std::shared_lock<std::shared_mutex> held(lock_);
    std::string path = normalize("/", absolute);
    for (int hops = 0; hops < 40; ++hops) {
        auto parts = split_path(path);
        std::string current;
        bool restarted = false;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string candidate = current + "/" + parts[i];
            bool last = i + 1 == parts.size();
            std::string target;
            bool is_link = false;
            if (!last || follow) {
                auto link = symlinks_.find(candidate);
                if (link != symlinks_.end()) {
                    target = link->second;
                    is_link = true;
                } else {
                    is_link = dynamic_link(candidate, target);
                }
            }
            if (is_link) {
                std::string base = current.empty() ? "/" : current;
                std::string next = normalize(base, target);
                for (size_t k = i + 1; k < parts.size(); ++k) next += "/" + parts[k];
                path = normalize("/", next);
                restarted = true;
                break;
            }
            current = candidate;
        }
        if (!restarted) return resolve_once(current.empty() ? "/" : current, out);
    }
    return -ELOOP_;
}

bool Vfs::readlink(const std::string& absolute, std::string& target) const {
    // Links in the parent directories are followed (/proc/self/fd/3).
    std::string path = normalize("/", absolute);
    size_t slash = path.rfind('/');
    if (slash != 0 && slash != std::string::npos) {
        Resolved parent;
        if (resolve(path.substr(0, slash), true, parent) == 0) path = (parent.guest == "/" ? "" : parent.guest) + path.substr(slash);
    }
    std::shared_lock<std::shared_mutex> held(lock_);
    auto it = symlinks_.find(path);
    if (it != symlinks_.end()) {
        target = it->second;
        return true;
    }
    return dynamic_link(path, target);
}

std::vector<std::pair<std::string, uint8_t>> Vfs::virtual_children(const std::string& dir) const {
    std::shared_lock<std::shared_mutex> held(lock_);
    std::string below = dir == "/" ? "/" : dir + "/";
    std::map<std::string, uint8_t> names;
    auto consider = [&](const std::string& full, uint8_t type) {
        if (full.size() <= below.size() || full.compare(0, below.size(), below) != 0) return;
        std::string rest = full.substr(below.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) names.emplace(rest, type);
        else names.emplace(rest.substr(0, slash), DT_DIR_);
    };
    for (const Mount& m : mounts_)
        if (!m.guest.empty()) consider(m.guest, m.is_file ? DT_REG_ : DT_DIR_);
    for (const auto& [name, reader] : synthetic_) consider(name, DT_REG_);
    for (const auto& [name, children] : synthetic_dirs_) consider(name, DT_DIR_);
    for (const auto& [name, target] : symlinks_) consider(name, DT_LNK_);
    for (const auto& [name, device] : devices_) consider(name, DT_CHR_);
    for (const auto& [name, resolver] : dynamic_) consider(name, DT_DIR_);
    if (auto it = synthetic_dirs_.find(dir == "/" ? "/" : dir); it != synthetic_dirs_.end())
        for (auto& child : it->second) names.emplace(child, DT_UNKNOWN_);
    return {names.begin(), names.end()};
}

}  // namespace axrb
