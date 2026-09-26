/* Lifecycle shutdown probe: no rendering or headset is needed. */
#include <android/native_activity.h>
#include <stdint.h>
extern void qb_check(const char*, uint64_t, uint64_t, uint64_t, uint64_t);
static int paused, stopped;
static void pause_activity(ANativeActivity* activity) { (void)activity; paused = 1; }
static void stop_activity(ANativeActivity* activity) {
    (void)activity;
    qb_check("pause before stop", paused, 0, 1, 0);
    stopped = 1;
}
static void destroy_activity(ANativeActivity* activity) {
    (void)activity;
    qb_check("stop before destroy", stopped, 0, 1, 0);
}
void ANativeActivity_onCreate(ANativeActivity* activity, void* saved, size_t size) {
    (void)saved; (void)size;
    activity->callbacks->onPause = pause_activity;
    activity->callbacks->onStop = stop_activity;
    activity->callbacks->onDestroy = destroy_activity;
}
