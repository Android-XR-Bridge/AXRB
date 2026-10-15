#pragma once

#include "files.h"

#include <functional>
#include <string>
#include <string_view>

namespace axrb {

// A connection to a service the host implements behind an AF_UNIX path
// (/dev/socket/logdw, property_service, dnsproxyd). Every write (or sendmsg)
// is handed to the handler as one message; replies queue up for read.
class ServiceSocket : public OpenFile {
public:
    using Handler = std::function<void(ServiceSocket&, std::string_view)>;
    explicit ServiceSocket(Handler handler) : handler_(std::move(handler)) {}
    int64_t read(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t write(GuestThread& t, uint64_t buf, uint64_t len) override;
    int64_t writev(GuestThread& t, const std::vector<linux_abi::kiovec>& vecs) override;
    int64_t stat(linux_abi::kstat& st) override;
    uint32_t poll_mask() override;
    WaitQueue* wait_queue() override { return &waiters_; }
    // Message-level entry used by sendmsg after gathering.
    int64_t send_message(std::string_view message);
    // For handlers.
    void reply(const void* data, size_t len);
    void finish();

private:
    Handler handler_;
    std::mutex lock_;
    std::string pending_;
    bool finished_ = false;
    WaitQueue waiters_;
};

// Registers a host service at an AF_UNIX path (abstract names start with '@').
// `make` is called once per connection and returns that connection's handler.
void register_unix_service(const std::string& path, std::function<ServiceSocket::Handler()> make);

// Guest -> host address rewriting: the emulator's host alias 10.0.2.2 is the
// local machine here.
void net_init();

}  // namespace axrb
