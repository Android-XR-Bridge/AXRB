#pragma once

#include <map>
#include <string>

namespace axrb {

// System properties. The property area bionic maps from /dev/__properties__
// lives in guest memory and is maintained by the host exactly as init's
// property service would (the trie, serials and futex wakes), so
// __system_property_get/_wait and setprop through /dev/socket/property_service
// all work unmodified.
void props_init(const std::wstring& sysroot_host, const std::map<std::string, std::string>& overrides);
std::string property_get(const std::string& name, const std::string& fallback = "");
bool property_set(const std::string& name, const std::string& value);

}  // namespace axrb
