#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace axrb {

// The guest file system: a Linux path namespace assembled from mounts.
//
//   /                 the Android system root (read-only, from the SDK image)
//   /data/app/<pkg>   the APK (in place) and its extracted native libraries
//   /data/data/<pkg>  private storage (saves), persistent across sessions
//   /sdcard           shared storage; Android/obb/<pkg> is the game's download folder
//   /proc, /sys       synthesized
//
// Host paths never leak into the guest, and guest names that Windows cannot
// store (':' '*' '?' '"' '<' '>' '|' '\\', trailing dots/spaces) are escaped
// into the private-use area so any Linux name round-trips.
struct Mount {
    std::string guest;      // normalized guest prefix, no trailing slash ("" for /)
    std::wstring host;      // host directory or file
    bool read_only = false;
    bool is_file = false;   // a single file mounted at `guest`
};

using SyntheticReader = std::function<std::string()>;

struct Resolved {
    enum Kind { None, Host, Synthetic, SyntheticDir, Device } kind = None;
    std::string guest;           // canonical guest path (symlinks resolved)
    std::wstring host;           // for Host
    const Mount* mount = nullptr;
    bool read_only = false;
    SyntheticReader reader;      // for Synthetic
    std::vector<std::string> children;  // for SyntheticDir
    std::string device;          // for Device (null, zero, urandom, ...)
};

class Vfs {
public:
    void add_mount(const std::string& guest, const std::wstring& host, bool read_only, bool is_file = false);
    void add_symlink(const std::string& guest, const std::string& target);
    void add_synthetic(const std::string& guest, SyntheticReader reader);
    void add_synthetic_dir(const std::string& guest, std::vector<std::string> children);
    void add_device(const std::string& guest, const std::string& device);
    void set_dynamic_dir(const std::string& prefix,
                         std::function<bool(const std::string& rest, Resolved& out)> resolver);

    // Normalizes `path` against `base` (an absolute directory) without
    // resolving symlinks.
    static std::string normalize(const std::string& base, const std::string& path);
    // Resolves symlinks in every component (the last one too, if follow).
    // Returns 0 or -errno.
    int resolve(const std::string& absolute, bool follow, Resolved& out) const;
    // The symlink target of `absolute` itself, if it is one we know.
    bool readlink(const std::string& absolute, std::string& target) const;
    // Lists names a directory has beyond what the host directory holds
    // (mount points, synthetic entries, symlinks).
    std::vector<std::pair<std::string, uint8_t>> virtual_children(const std::string& dir) const;

    std::wstring host_path_for(const Mount& mount, const std::string& guest) const;

private:
    int resolve_once(const std::string& path, Resolved& out) const;
    bool dynamic_link(const std::string& path, std::string& target) const;

    mutable std::shared_mutex lock_;
    std::vector<Mount> mounts_;  // sorted longest guest prefix first
    std::map<std::string, std::string> symlinks_;
    std::map<std::string, SyntheticReader> synthetic_;
    std::map<std::string, std::vector<std::string>> synthetic_dirs_;
    std::map<std::string, std::string> devices_;
    std::vector<std::pair<std::string, std::function<bool(const std::string&, Resolved&)>>> dynamic_;
};

Vfs& vfs();

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
// Linux file name component <-> Windows file name component.
std::wstring escape_component(const std::string& name);
std::string unescape_component(const std::wstring& name);

}  // namespace axrb
