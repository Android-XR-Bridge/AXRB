#pragma once
#include <mutex>

// Vulkan queue operations require external synchronization. The guest's
// submissions and the bridge compositor share the same host queue.
inline std::recursive_mutex &guest_vulkan_queue_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}
