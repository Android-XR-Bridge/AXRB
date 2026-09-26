/* The Android UI thread, for the work Java code posts to it.

   Unity's Java side sends Runnables to the UI thread (Activity.runOnUiThread,
   Handler.post) and native code often waits for them to finish, so dropping
   them hangs the engine. The Runnables Unity makes are JNIBridge proxies whose
   run() calls back into native code, so running one here means calling that
   native entry point, on a thread of its own, as the real UI thread would.

   The thread is a guest thread like any pthread: its own stack and thread
   local block out of the thread area, and a host thread driving the
   interpreter over it, one posted job at a time. */

#include "qb_env.h"
#include "android.h"

#include <chrono>
#include <cstdio>
#include <cstring>

void GuestLibc::post_to_ui(uint64_t function, const std::vector<uint64_t>& args, int delay_ms) {
    std::unique_lock<std::mutex> held(ui_lock);
    if (!ui_started) {
        ui_started = true;
        const uint64_t kStack = 1u << 20;
        uint64_t tls_bytes = (image->linker.tls_used + 15) & ~15ull;
        {
            std::lock_guard<std::recursive_mutex> area(lock);
            ui_stack_va = thread_area_va + thread_area_used;
            thread_area_used += kStack;
            ui_tls_va = thread_area_va + thread_area_used;
            thread_area_used += tls_bytes + (64u << 10); /* room for libraries opened later */
            std::memcpy(thread_area.data() + (ui_tls_va - thread_area_va), image->linker.tls_initial.data(),
                        (size_t)std::min<uint64_t>(tls_bytes, image->linker.tls_initial.size()));
        }
        ui_stack_top = ui_stack_va + kStack - 64;
        std::thread([this] {
            for (;;) {
                UiJob job;
                {
                    std::unique_lock<std::mutex> wait(ui_lock);
                    ui_ready.wait(wait, [this] { return !ui_jobs.empty(); });
                    job = ui_jobs.front();
                    ui_jobs.pop_front();
                }
                auto now = std::chrono::steady_clock::now();
                if (job.due > now) std::this_thread::sleep_until(job.due);
                GuestCpu cpu{};
                for (size_t i = 0; i < job.args.size() && i < 8; ++i) cpu.x[i] = job.args[i];
                cpu.sp = ui_stack_top;
                cpu.tpidr = ui_tls_va;
                cpu.x[30] = 1;
                cpu.pc = job.function;
                if (QB_ENV("QB_TRACE")) std::printf("ui: running %s\n", guest_describe(job.function).c_str());
                guest_run(cpu, image->mem, image->callback, image->callback_user, 0);
            }
        }).detach();
    }
    UiJob job;
    job.function = function;
    job.args = args;
    job.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    ui_jobs.push_back(job);
    held.unlock();
    ui_ready.notify_one();
}
