#include "props.h"

#include "files.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"
#include "net.h"
#include "vfs.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

namespace axrb {
using namespace linux_abi;

namespace {

// bionic/libc/system_properties: prop_area, prop_bt, prop_info.
constexpr uint32_t kAreaMagic = 0x504f5250;
constexpr uint32_t kAreaVersion = 0xfc6ed0ab;
constexpr uint32_t kHeaderSize = 128;   // bytes_used, serial, magic, version, reserved[28]
constexpr uint32_t kBtSize = 20;        // namelen, prop, left, right, children
constexpr uint32_t kValueMax = 92;      // PROP_VALUE_MAX
constexpr uint32_t kInfoSize = 4 + kValueMax;
constexpr uint32_t kLongFlag = 1u << 16;
constexpr char kLongLegacyError[] = "Must use __system_property_read_callback() to read";
constexpr uint64_t kAreaSize = 1ull << 20;

std::mutex g_lock;
uint8_t* g_area = nullptr;

uint8_t* data() { return g_area + kHeaderSize; }
uint32_t& bytes_used() { return *reinterpret_cast<uint32_t*>(g_area); }
std::atomic<uint32_t>& area_serial() { return *reinterpret_cast<std::atomic<uint32_t>*>(g_area + 4); }
std::atomic<uint32_t>& field(uint32_t offset) { return *reinterpret_cast<std::atomic<uint32_t>*>(data() + offset); }

uint32_t allocate(uint32_t size) {
    uint32_t aligned = (size + 3) & ~3u;
    if (bytes_used() + aligned > kAreaSize - kHeaderSize) return 0;
    uint32_t offset = bytes_used();
    bytes_used() += aligned;
    return offset;
}

uint32_t new_bt(const char* name, uint32_t len) {
    uint32_t offset = allocate(kBtSize + len + 1);
    if (!offset) return 0;
    uint8_t* bt = data() + offset;
    std::memcpy(bt, &len, 4);
    std::memcpy(bt + kBtSize, name, len);
    bt[kBtSize + len] = 0;
    return offset;
}

uint32_t new_info(const std::string& name, const std::string& value) {
    uint32_t offset = allocate(kInfoSize + static_cast<uint32_t>(name.size()) + 1);
    if (!offset) return 0;
    uint8_t* info = data() + offset;
    std::memcpy(info + kInfoSize, name.c_str(), name.size() + 1);
    if (value.size() >= kValueMax) {
        uint32_t long_offset = allocate(static_cast<uint32_t>(value.size()) + 1);
        if (!long_offset) return 0;
        std::memcpy(data() + long_offset, value.c_str(), value.size() + 1);
        std::memcpy(info + 4, kLongLegacyError, sizeof kLongLegacyError);
        uint32_t relative = long_offset - offset;
        std::memcpy(info + 4 + 56, &relative, 4);
        field(offset).store(kLongFlag | static_cast<uint32_t>(sizeof kLongLegacyError - 1) << 24, std::memory_order_release);
    } else {
        std::memcpy(info + 4, value.c_str(), value.size() + 1);
        field(offset).store(static_cast<uint32_t>(value.size()) << 24, std::memory_order_release);
    }
    return offset;
}

int compare(const char* one, uint32_t one_len, const uint8_t* bt) {
    uint32_t two_len;
    std::memcpy(&two_len, bt, 4);
    if (one_len < two_len) return -1;
    if (one_len > two_len) return 1;
    return std::strncmp(one, reinterpret_cast<const char*>(bt + kBtSize), one_len);
}

// One level of the trie: a binary tree ordered by (length, bytes).
uint32_t find_bt(uint32_t root, const char* name, uint32_t len, bool create) {
    uint32_t current = root;
    for (;;) {
        int r = compare(name, len, data() + current);
        if (r == 0) return current;
        uint32_t link = current + (r < 0 ? 8 : 12);
        uint32_t next = field(link).load(std::memory_order_acquire);
        if (!next) {
            if (!create) return 0;
            next = new_bt(name, len);
            if (!next) return 0;
            field(link).store(next, std::memory_order_release);
            return next;
        }
        current = next;
    }
}

// Offset of the prop_info for `name` (creating the path and the info).
uint32_t find_info(const std::string& name, const std::string* create_value) {
    uint32_t current = 0;  // the root node
    const char* remaining = name.c_str();
    for (;;) {
        const char* dot = std::strchr(remaining, '.');
        uint32_t len = dot ? static_cast<uint32_t>(dot - remaining) : static_cast<uint32_t>(std::strlen(remaining));
        uint32_t children = field(current + 16).load(std::memory_order_acquire);
        if (!children) {
            if (!create_value) return 0;
            children = new_bt(remaining, len);
            if (!children) return 0;
            field(current + 16).store(children, std::memory_order_release);
            current = children;
        } else {
            current = find_bt(children, remaining, len, create_value != nullptr);
            if (!current) return 0;
        }
        if (!dot) break;
        remaining = dot + 1;
    }
    uint32_t info = field(current + 4).load(std::memory_order_acquire);
    if (info || !create_value) return info;
    info = new_info(name, *create_value);
    if (info) field(current + 4).store(info, std::memory_order_release);
    return info;
}

std::string read_value(uint32_t info) {
    uint32_t serial = field(info).load(std::memory_order_acquire);
    const char* base = reinterpret_cast<const char*>(data() + info);
    if (serial & kLongFlag) {
        uint32_t relative;
        std::memcpy(&relative, base + 4 + 56, 4);
        return std::string(base + relative);
    }
    return std::string(base + 4, serial >> 24);
}

bool set_locked(const std::string& name, const std::string& value) {
    if (name.empty() || name.size() > 256) return false;
    uint32_t info = find_info(name, nullptr);
    if (!info) {
        if (!find_info(name, &value)) {
            LOGE("props", "property area full adding %s", name.c_str());
            return false;
        }
    } else {
        uint32_t serial = field(info).load(std::memory_order_relaxed);
        if (serial & kLongFlag) return false;  // read-only long values
        if (name.rfind("ro.", 0) == 0) return false;
        std::string stored = value.substr(0, kValueMax - 1);
        // Readers that see the dirty bit read the old value from the backup
        // area right after the root node.
        char* backup = reinterpret_cast<char*>(data() + kBtSize);
        std::memcpy(backup, data() + info + 4, kValueMax);
        field(info).store(serial | 1, std::memory_order_release);
        std::memcpy(data() + info + 4, stored.c_str(), stored.size() + 1);
        field(info).store((static_cast<uint32_t>(stored.size()) << 24) | ((serial + 1) & 0xffffff), std::memory_order_release);
        wake_futex(reinterpret_cast<uint64_t>(data() + info), 1 << 30);
    }
    area_serial().fetch_add(1, std::memory_order_release);
    wake_futex(reinterpret_cast<uint64_t>(g_area + 4), 1 << 30);
    return true;
}

// /dev/__properties__ as bionic opens it (single-file "pre-split" layout).
class PropertyAreaFile : public OpenFile {
public:
    int64_t read(GuestThread&, uint64_t, uint64_t) override { return 0; }
    int64_t stat(kstat& st) override {
        std::memset(&st, 0, sizeof st);
        st.st_mode = S_IFREG_ | 0444;  // root-owned, not group/other writable: bionic checks
        st.st_nlink = 1;
        st.st_size = static_cast<int64_t>(kAreaSize);
        st.st_blksize = 4096;
        st.st_ino = 0x50524f50;
        return 0;
    }
    uint64_t fixed_mapping() const override { return reinterpret_cast<uint64_t>(g_area); }
};

void load_prop_file(const std::wstring& path, std::map<std::string, std::string>& out) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#' || line.rfind("import ", 0) == 0) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::string host_timezone() {
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    if (GetDynamicTimeZoneInformation(&info) == TIME_ZONE_ID_INVALID) return "UTC";
    // Windows 10 ships ICU; it maps Windows zone names to IANA ones.
    using Fn = int32_t(__cdecl*)(const wchar_t*, int32_t, const char*, wchar_t*, int32_t, int*);
    static HMODULE icu = LoadLibraryW(L"icu.dll");
    if (!icu) return "UTC";
    auto fn = reinterpret_cast<Fn>(GetProcAddress(icu, "ucal_getTimeZoneIDForWindowsID"));
    if (!fn) return "UTC";
    wchar_t id[128] = {};
    int status = 0;
    int32_t n = fn(info.TimeZoneKeyName, -1, nullptr, id, 127, &status);
    if (status > 0 || n <= 0) return "UTC";
    return narrow(std::wstring(id, static_cast<size_t>(n)));
}

ServiceSocket::Handler property_service() {
    auto buffer = std::make_shared<std::string>();
    return [buffer](ServiceSocket& socket, std::string_view message) {
        buffer->append(message);
        if (buffer->size() < 4) return;
        uint32_t command;
        std::memcpy(&command, buffer->data(), 4);
        if (command == 0x00020001) {  // PROP_MSG_SETPROP2: len-prefixed name and value
            auto take = [&](size_t& at, std::string& out) {
                if (buffer->size() < at + 4) return false;
                uint32_t len;
                std::memcpy(&len, buffer->data() + at, 4);
                if (buffer->size() < at + 4 + len) return false;
                out.assign(buffer->data() + at + 4, len);
                at += 4 + len;
                return true;
            };
            size_t at = 4;
            std::string name, value;
            if (!take(at, name) || !take(at, value)) return;
            buffer->erase(0, at);
            bool ok = property_set(name, value);
            LOGD("props", "setprop %s=%s%s", name.c_str(), value.c_str(), ok ? "" : " (refused)");
            uint32_t result = ok ? 0 : 0x18;  // PROP_ERROR_SET_FAILED
            socket.reply(&result, 4);
        } else if (command == 1) {  // PROP_MSG_SETPROP: fixed 128-byte record
            if (buffer->size() < 128) return;
            std::string name(buffer->data() + 4, strnlen(buffer->data() + 4, 32));
            std::string value(buffer->data() + 36, strnlen(buffer->data() + 36, 92));
            buffer->erase(0, 128);
            property_set(name, value);
            socket.finish();
        } else {
            buffer->clear();
            socket.finish();
        }
    };
}

}  // namespace

std::string property_get(const std::string& name, const std::string& fallback) {
    std::lock_guard<std::mutex> held(g_lock);
    if (!g_area) return fallback;
    uint32_t info = find_info(name, nullptr);
    return info ? read_value(info) : fallback;
}

bool property_set(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> held(g_lock);
    if (!g_area) return false;
    if (name.rfind("ctl.", 0) == 0) return true;  // service control: nothing to start or stop
    return set_locked(name, value);
}

void props_init(const std::wstring& sysroot_host, const std::map<std::string, std::string>& overrides) {
    int64_t at = address_space().mmap(0, kAreaSize, PROT_READ_ | PROT_WRITE_, MAP_SHARED_ | MAP_ANONYMOUS_, nullptr, 0,
                                      "/dev/__properties__");
    if (at < 0) fatal("cannot map the property area");
    address_space().populate(static_cast<uint64_t>(at), kAreaSize);
    g_area = reinterpret_cast<uint8_t*>(at);
    std::memset(g_area, 0, kAreaSize);
    std::memcpy(g_area + 8, &kAreaMagic, 4);
    std::memcpy(g_area + 12, &kAreaVersion, 4);
    bytes_used() = kBtSize + kValueMax;  // the root node and the dirty backup area

    std::map<std::string, std::string> props;
    load_prop_file(sysroot_host + L"\\system\\build.prop", props);
    load_prop_file(sysroot_host + L"\\vendor-build.prop", props);

    // What init derives at boot on the emulator image the games are known to
    // run on (ro.hardware=ranchu also selects AXRB's loopback transport).
    for (const char* key : {"brand", "device", "manufacturer", "model", "name"}) {
        std::string vendor = props["ro.product.vendor." + std::string(key)];
        if (!vendor.empty()) props["ro.product." + std::string(key)] = vendor;
    }
    props["ro.product.model"] = "sdk_gphone64_arm64";
    props["ro.product.manufacturer"] = "Google";
    props["ro.product.brand"] = "google";
    props["ro.product.name"] = "sdk_gphone64_arm64";
    props["ro.product.cpu.abi"] = "arm64-v8a";
    props["ro.product.cpu.abilist"] = "arm64-v8a";
    props["ro.product.cpu.abilist64"] = "arm64-v8a";
    props["ro.product.cpu.abilist32"] = "";
    props["ro.hardware"] = "ranchu";
    props["ro.boot.hardware"] = "ranchu";
    props["ro.boot.qemu"] = "1";
    props["ro.arch"] = "arm64";
    props["ro.build.fingerprint"] = props.count("ro.system.build.fingerprint") ? props["ro.system.build.fingerprint"] : "";
    props["ro.serialno"] = "AXRBDIRECT0001";
    props["ro.boot.serialno"] = "AXRBDIRECT0001";
    props["ro.debuggable"] = "0";
    props["ro.secure"] = "1";
    props["ro.property_service.version"] = "2";
    props["ro.opengles.version"] = "196610";
    props["ro.sf.lcd_density"] = "440";
    props["sys.boot_completed"] = "1";
    props["dev.bootcomplete"] = "1";
    props["persist.sys.locale"] = "en-US";
    props["persist.sys.timezone"] = host_timezone();
    props["persist.sys.dalvik.vm.lib.2"] = "libart.so";
    props["ro.dalvik.vm.native.bridge"] = "0";
    for (auto& [name, value] : overrides) props[name] = value;

    {
        std::lock_guard<std::mutex> held(g_lock);
        for (auto& [name, value] : props) {
            if (!find_info(name, &value)) LOGW("props", "could not add %s", name.c_str());
        }
    }
    LOGI("props", "%zu properties, %u bytes of %llu", props.size(), bytes_used(), (unsigned long long)kAreaSize);

    register_device("properties", [] { return std::make_shared<PropertyAreaFile>(); });
    vfs().add_device("/dev/__properties__", "properties");
    register_unix_service("/dev/socket/property_service", property_service);
}

}  // namespace axrb
