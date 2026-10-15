#pragma once

#include <memory>
#include <string>

namespace arun::vm {
class Runtime;
}
namespace arun::pkg {
class ZipReader;
}

namespace axrb {

// Registers the host natives of boot classes backed by guest state (direct
// buffers, file descriptors, APK assets opened as descriptors).
void register_direct_natives(arun::vm::Runtime* rt, std::shared_ptr<arun::pkg::ZipReader> apk, std::string guest_apk);

}  // namespace axrb
