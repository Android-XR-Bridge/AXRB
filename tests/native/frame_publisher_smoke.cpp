#include "../../runtime/src/frame_publisher.h"
#include <cassert>
#include <future>
#include <stdexcept>

int main() {
    axrb::runtime::FramePublisher publisher;
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    int published = 0;
    assert(publisher.submit([&] { entered.set_value(); released.wait(); ++published; return true; }));
    entered.get_future().wait();
    // No unbounded queue and no reuse while an ACK/fence wait is pending.
    assert(!publisher.submit([] { return true; }));
    release.set_value();
    assert(publisher.drain());
    assert(published == 1);
    assert(publisher.submit([]() -> bool { throw std::runtime_error("transport failed"); }));
    assert(!publisher.drain());
    assert(publisher.submit([&] { ++published; return true; }));
    assert(publisher.drain());
    assert(published == 2);
    // Destruction joins the outstanding publication before captured state dies.
    {
        axrb::runtime::FramePublisher scoped;
        assert(scoped.submit([&] { ++published; return true; }));
    }
    assert(published == 3);
}
