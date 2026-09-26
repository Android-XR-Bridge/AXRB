/* A native application built the way Quest applications are built: the NDK's
   own android_native_app_glue, unmodified, with an android_main that waits on
   the looper for the lifecycle to happen.

   Nothing in this file works around the emulation. The glue makes a pipe,
   starts a thread, and blocks until the Java side tells it the application
   has started, gained a window and gained focus. */

#include <android_native_app_glue.h>
#include <stdint.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

static int seen_start = 0;
static int seen_resume = 0;
static int seen_window = 0;
static int seen_focus = 0;

static void on_command(struct android_app* app, int32_t command) {
    (void)app;
    switch (command) {
        case APP_CMD_START: seen_start = 1; break;
        case APP_CMD_RESUME: seen_resume = 1; break;
        case APP_CMD_INIT_WINDOW: seen_window = 1; break;
        case APP_CMD_GAINED_FOCUS: seen_focus = 1; break;
        default: break;
    }
}

void android_main(struct android_app* app) {
    CHECK("android_main was reached", app != 0, 1);
    CHECK("the activity came with it", app->activity != 0, 1);
    CHECK("and a virtual machine", app->activity->vm != 0, 1);
    CHECK("and an internal data path", app->activity->internalDataPath != 0, 1);
    CHECK("and a plausible sdk version", app->activity->sdkVersion >= 21, 1);

    app->onAppCmd = on_command;

    /* Wait for the lifecycle, the way a real application does. */
    for (int spins = 0; spins < 400 && !(seen_start && seen_resume && seen_window && seen_focus); ++spins) {
        int events = 0;
        struct android_poll_source* source = 0;
        if (ALooper_pollOnce(10, NULL, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }
    }

    CHECK("the application was started", seen_start, 1);
    CHECK("and resumed", seen_resume, 1);
    CHECK("and given a window", seen_window, 1);
    CHECK("and given focus", seen_focus, 1);
    CHECK("the window is the one that was sent", app->window != 0, 1);
    CHECK("its width", ANativeWindow_getWidth(app->window), 1280);
    CHECK("its height", ANativeWindow_getHeight(app->window), 720);
}
