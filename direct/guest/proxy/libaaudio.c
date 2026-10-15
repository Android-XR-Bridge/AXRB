// libaaudio.so for AXRB Direct.
//
// The API state lives here; the samples go to the host's default output device
// (WASAPI, direct/src/audio.cpp). Data callbacks run on a guest thread of this
// library that waits for room in the host buffer, so applications see the
// AAudio threading they expect. There is no input device.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../axrb_guest.h"

typedef int32_t aaudio_result_t;
typedef int32_t aaudio_stream_state_t;
typedef int32_t aaudio_format_t;
typedef int32_t aaudio_direction_t;
typedef int32_t aaudio_sharing_mode_t;
typedef int32_t aaudio_performance_mode_t;
typedef int32_t aaudio_data_callback_result_t;

enum {
    AAUDIO_OK = 0,
    AAUDIO_ERROR_BASE = -900,
    AAUDIO_ERROR_DISCONNECTED = -899,
    AAUDIO_ERROR_ILLEGAL_ARGUMENT = -898,
    AAUDIO_ERROR_INTERNAL = -896,
    AAUDIO_ERROR_INVALID_STATE = -895,
    AAUDIO_ERROR_INVALID_HANDLE = -892,
    AAUDIO_ERROR_UNIMPLEMENTED = -890,
    AAUDIO_ERROR_UNAVAILABLE = -889,
    AAUDIO_ERROR_NO_FREE_HANDLES = -888,
    AAUDIO_ERROR_NO_MEMORY = -887,
    AAUDIO_ERROR_NULL = -886,
    AAUDIO_ERROR_TIMEOUT = -885,
    AAUDIO_ERROR_WOULD_BLOCK = -884,
    AAUDIO_ERROR_INVALID_FORMAT = -883,
    AAUDIO_ERROR_OUT_OF_RANGE = -882,
    AAUDIO_ERROR_NO_SERVICE = -881,
    AAUDIO_ERROR_INVALID_RATE = -880,
};

enum {
    AAUDIO_STREAM_STATE_UNINITIALIZED = 0,
    AAUDIO_STREAM_STATE_UNKNOWN,
    AAUDIO_STREAM_STATE_OPEN,
    AAUDIO_STREAM_STATE_STARTING,
    AAUDIO_STREAM_STATE_STARTED,
    AAUDIO_STREAM_STATE_PAUSING,
    AAUDIO_STREAM_STATE_PAUSED,
    AAUDIO_STREAM_STATE_FLUSHING,
    AAUDIO_STREAM_STATE_FLUSHED,
    AAUDIO_STREAM_STATE_STOPPING,
    AAUDIO_STREAM_STATE_STOPPED,
    AAUDIO_STREAM_STATE_CLOSING,
    AAUDIO_STREAM_STATE_CLOSED,
    AAUDIO_STREAM_STATE_DISCONNECTED,
};

enum { AAUDIO_DIRECTION_OUTPUT = 0, AAUDIO_DIRECTION_INPUT = 1 };
enum {
    AAUDIO_FORMAT_INVALID = -1,
    AAUDIO_FORMAT_UNSPECIFIED = 0,
    AAUDIO_FORMAT_PCM_I16 = 1,
    AAUDIO_FORMAT_PCM_FLOAT = 2,
    AAUDIO_FORMAT_PCM_I24_PACKED = 3,
    AAUDIO_FORMAT_PCM_I32 = 4,
};
enum { AAUDIO_SHARING_MODE_EXCLUSIVE = 0, AAUDIO_SHARING_MODE_SHARED = 1 };
enum { AAUDIO_PERFORMANCE_MODE_NONE = 10, AAUDIO_PERFORMANCE_MODE_POWER_SAVING = 11, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12 };
enum { AAUDIO_CALLBACK_RESULT_CONTINUE = 0, AAUDIO_CALLBACK_RESULT_STOP = 1 };
#define AAUDIO_UNSPECIFIED 0

typedef struct AAudioStreamStruct AAudioStream;
typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(AAudioStream* stream, void* userData,
                                                                   void* audioData, int32_t numFrames);
typedef void (*AAudioStream_errorCallback)(AAudioStream* stream, void* userData, aaudio_result_t error);

typedef struct AAudioStreamBuilderStruct {
    int32_t device_id, sample_rate, channel_count, buffer_capacity, frames_per_callback, session_id;
    aaudio_format_t format;
    aaudio_direction_t direction;
    aaudio_sharing_mode_t sharing_mode;
    aaudio_performance_mode_t performance_mode;
    int32_t usage, content_type, input_preset, capture_policy, privacy_sensitive;
    uint32_t channel_mask;
    AAudioStream_dataCallback data_callback;
    void* data_user;
    AAudioStream_errorCallback error_callback;
    void* error_user;
} AAudioStreamBuilder;

struct AAudioStreamStruct {
    AAudioStreamBuilder params;
    int64_t host;  // host stream handle
    int32_t burst;
    int32_t buffer_size;
    int32_t frame_bytes;
    volatile aaudio_stream_state_t state;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    int thread_running;
    volatile int stop_thread;
    int64_t frames_written;
    void* callback_buffer;
    int32_t callback_capacity;
};

// Host side (direct/src/audio.cpp).
extern int64_t axrb_audio_open(int32_t sample_rate, int32_t channels, int32_t format, int32_t* out_rate,
                               int32_t* out_burst, int32_t* out_capacity);
extern int32_t axrb_audio_start(int64_t handle);
extern int32_t axrb_audio_stop(int64_t handle);
extern int32_t axrb_audio_flush(int64_t handle);
extern int32_t axrb_audio_wait(int64_t handle, int64_t timeout_ns);
extern int32_t axrb_audio_write(int64_t handle, const void* data, int32_t frames);
extern int32_t axrb_audio_position(int64_t handle, int64_t* frames, int64_t* time_ns);
extern void axrb_audio_close(int64_t handle);
AXRB_HOST_FUNCTION(axrb_audio_open, "audio.open");
AXRB_HOST_FUNCTION(axrb_audio_start, "audio.start");
AXRB_HOST_FUNCTION(axrb_audio_stop, "audio.stop");
AXRB_HOST_FUNCTION(axrb_audio_flush, "audio.flush");
AXRB_HOST_FUNCTION(axrb_audio_wait, "audio.wait");
AXRB_HOST_FUNCTION(axrb_audio_write, "audio.write");
AXRB_HOST_FUNCTION(axrb_audio_position, "audio.position");
AXRB_HOST_FUNCTION(axrb_audio_close, "audio.close");

static int32_t sample_bytes(aaudio_format_t f) {
    switch (f) {
        case AAUDIO_FORMAT_PCM_I16: return 2;
        case AAUDIO_FORMAT_PCM_I24_PACKED: return 3;
        default: return 4;
    }
}

const char* AAudio_convertResultToText(aaudio_result_t r) {
    switch (r) {
        case AAUDIO_OK: return "AAUDIO_OK";
        case AAUDIO_ERROR_DISCONNECTED: return "AAUDIO_ERROR_DISCONNECTED";
        case AAUDIO_ERROR_ILLEGAL_ARGUMENT: return "AAUDIO_ERROR_ILLEGAL_ARGUMENT";
        case AAUDIO_ERROR_INTERNAL: return "AAUDIO_ERROR_INTERNAL";
        case AAUDIO_ERROR_INVALID_STATE: return "AAUDIO_ERROR_INVALID_STATE";
        case AAUDIO_ERROR_INVALID_HANDLE: return "AAUDIO_ERROR_INVALID_HANDLE";
        case AAUDIO_ERROR_UNIMPLEMENTED: return "AAUDIO_ERROR_UNIMPLEMENTED";
        case AAUDIO_ERROR_UNAVAILABLE: return "AAUDIO_ERROR_UNAVAILABLE";
        case AAUDIO_ERROR_NO_MEMORY: return "AAUDIO_ERROR_NO_MEMORY";
        case AAUDIO_ERROR_NULL: return "AAUDIO_ERROR_NULL";
        case AAUDIO_ERROR_TIMEOUT: return "AAUDIO_ERROR_TIMEOUT";
        case AAUDIO_ERROR_WOULD_BLOCK: return "AAUDIO_ERROR_WOULD_BLOCK";
        case AAUDIO_ERROR_INVALID_FORMAT: return "AAUDIO_ERROR_INVALID_FORMAT";
        case AAUDIO_ERROR_OUT_OF_RANGE: return "AAUDIO_ERROR_OUT_OF_RANGE";
        case AAUDIO_ERROR_NO_SERVICE: return "AAUDIO_ERROR_NO_SERVICE";
        case AAUDIO_ERROR_INVALID_RATE: return "AAUDIO_ERROR_INVALID_RATE";
        default: return "Unrecognized AAudio error.";
    }
}

const char* AAudio_convertStreamStateToText(aaudio_stream_state_t s) {
    static const char* names[] = {"UNINITIALIZED", "UNKNOWN", "OPEN", "STARTING", "STARTED", "PAUSING", "PAUSED",
                                  "FLUSHING", "FLUSHED", "STOPPING", "STOPPED", "CLOSING", "CLOSED", "DISCONNECTED"};
    return s >= 0 && s < (int)(sizeof names / sizeof names[0]) ? names[s] : "UNKNOWN";
}

// --- builder ----------------------------------------------------------------------------------

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** builder) {
    AAudioStreamBuilder* b = calloc(1, sizeof *b);
    if (!b) return AAUDIO_ERROR_NO_MEMORY;
    b->sharing_mode = AAUDIO_SHARING_MODE_SHARED;
    b->performance_mode = AAUDIO_PERFORMANCE_MODE_NONE;
    b->direction = AAUDIO_DIRECTION_OUTPUT;
    b->usage = 1;
    b->content_type = 2;
    b->input_preset = 6;
    b->capture_policy = 1;
    *builder = b;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder* builder) {
    free(builder);
    return AAUDIO_OK;
}

#define SETTER(name, field, type) \
    void AAudioStreamBuilder_##name(AAudioStreamBuilder* b, type v) { b->field = v; }
SETTER(setDeviceId, device_id, int32_t)
SETTER(setSampleRate, sample_rate, int32_t)
SETTER(setChannelCount, channel_count, int32_t)
SETTER(setSamplesPerFrame, channel_count, int32_t)
SETTER(setFormat, format, aaudio_format_t)
SETTER(setSharingMode, sharing_mode, aaudio_sharing_mode_t)
SETTER(setDirection, direction, aaudio_direction_t)
SETTER(setBufferCapacityInFrames, buffer_capacity, int32_t)
SETTER(setPerformanceMode, performance_mode, aaudio_performance_mode_t)
SETTER(setUsage, usage, int32_t)
SETTER(setContentType, content_type, int32_t)
SETTER(setInputPreset, input_preset, int32_t)
SETTER(setAllowedCapturePolicy, capture_policy, int32_t)
SETTER(setSessionId, session_id, int32_t)
SETTER(setPrivacySensitive, privacy_sensitive, int32_t)
SETTER(setFramesPerDataCallback, frames_per_callback, int32_t)
SETTER(setChannelMask, channel_mask, uint32_t)
void AAudioStreamBuilder_setPackageName(AAudioStreamBuilder* b, const char* name) {}
void AAudioStreamBuilder_setAttributionTag(AAudioStreamBuilder* b, const char* tag) {}
void AAudioStreamBuilder_setIsContentSpatialized(AAudioStreamBuilder* b, int v) {}
void AAudioStreamBuilder_setSpatializationBehavior(AAudioStreamBuilder* b, int32_t v) {}

void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder* b, AAudioStream_dataCallback callback, void* user) {
    b->data_callback = callback;
    b->data_user = user;
}

void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder* b, AAudioStream_errorCallback callback, void* user) {
    b->error_callback = callback;
    b->error_user = user;
}

static int32_t channels_of(const AAudioStreamBuilder* b) {
    if (b->channel_count > 0) return b->channel_count;
    if (b->channel_mask) return __builtin_popcount(b->channel_mask);
    return 2;
}

aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b, AAudioStream** out) {
    if (!out) return AAUDIO_ERROR_NULL;
    *out = NULL;
    if (b->direction == AAUDIO_DIRECTION_INPUT) return AAUDIO_ERROR_UNAVAILABLE;
    AAudioStream* s = calloc(1, sizeof *s);
    if (!s) return AAUDIO_ERROR_NO_MEMORY;
    s->params = *b;
    s->params.channel_count = channels_of(b);
    if (s->params.format == AAUDIO_FORMAT_UNSPECIFIED) s->params.format = AAUDIO_FORMAT_PCM_FLOAT;
    int32_t rate = 0, burst = 0, capacity = 0;
    int64_t h = axrb_audio_open(s->params.sample_rate, s->params.channel_count, s->params.format, &rate, &burst,
                                &capacity);
    if (h <= 0) {
        free(s);
        return h < 0 ? (aaudio_result_t)h : AAUDIO_ERROR_UNAVAILABLE;
    }
    s->host = h;
    s->params.sample_rate = rate;
    s->params.sharing_mode = AAUDIO_SHARING_MODE_SHARED;
    if (s->params.session_id > 0) s->params.session_id = 1;
    s->burst = burst;
    s->params.buffer_capacity = capacity;
    s->buffer_size = capacity;
    s->frame_bytes = sample_bytes(s->params.format) * s->params.channel_count;
    s->state = AAUDIO_STREAM_STATE_OPEN;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->changed, NULL);
    *out = s;
    return AAUDIO_OK;
}

// --- stream -----------------------------------------------------------------------------------

static void set_state(AAudioStream* s, aaudio_stream_state_t state) {
    pthread_mutex_lock(&s->lock);
    s->state = state;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->lock);
}

static void* callback_thread(void* arg) {
    AAudioStream* s = arg;
    int32_t frames = s->params.frames_per_callback > 0 ? s->params.frames_per_callback : s->burst;
    if (s->callback_capacity < frames) {
        free(s->callback_buffer);
        s->callback_buffer = malloc((size_t)frames * s->frame_bytes);
        s->callback_capacity = frames;
    }
    while (!s->stop_thread) {
        int32_t room = axrb_audio_wait(s->host, 50 * 1000000LL);
        if (room < 0) {
            if (s->params.error_callback) s->params.error_callback(s, s->params.error_user, AAUDIO_ERROR_DISCONNECTED);
            set_state(s, AAUDIO_STREAM_STATE_DISCONNECTED);
            break;
        }
        while (!s->stop_thread && room >= frames) {
            aaudio_data_callback_result_t r =
                s->params.data_callback(s, s->params.data_user, s->callback_buffer, frames);
            axrb_audio_write(s->host, s->callback_buffer, frames);
            s->frames_written += frames;
            room -= frames;
            if (r == AAUDIO_CALLBACK_RESULT_STOP) {
                s->stop_thread = 1;
                axrb_audio_stop(s->host);
                set_state(s, AAUDIO_STREAM_STATE_STOPPED);
            }
        }
    }
    return NULL;
}

static void join_callback_thread(AAudioStream* s) {
    if (!s->thread_running) return;
    s->stop_thread = 1;
    if (!pthread_equal(pthread_self(), s->thread)) pthread_join(s->thread, NULL);
    else pthread_detach(s->thread);
    s->thread_running = 0;
}

aaudio_result_t AAudioStream_requestStart(AAudioStream* s) {
    if (s->state == AAUDIO_STREAM_STATE_CLOSED) return AAUDIO_ERROR_INVALID_STATE;
    if (s->state == AAUDIO_STREAM_STATE_STARTED) return AAUDIO_OK;
    int32_t r = axrb_audio_start(s->host);
    if (r < 0) return r;
    set_state(s, AAUDIO_STREAM_STATE_STARTED);
    if (s->params.data_callback && !s->thread_running) {
        s->stop_thread = 0;
        if (pthread_create(&s->thread, NULL, callback_thread, s) == 0) s->thread_running = 1;
    }
    return AAUDIO_OK;
}

static aaudio_result_t halt(AAudioStream* s, aaudio_stream_state_t final_state) {
    if (s->state == AAUDIO_STREAM_STATE_CLOSED) return AAUDIO_ERROR_INVALID_STATE;
    join_callback_thread(s);
    axrb_audio_stop(s->host);
    set_state(s, final_state);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestStop(AAudioStream* s) { return halt(s, AAUDIO_STREAM_STATE_STOPPED); }
aaudio_result_t AAudioStream_requestPause(AAudioStream* s) { return halt(s, AAUDIO_STREAM_STATE_PAUSED); }

aaudio_result_t AAudioStream_requestFlush(AAudioStream* s) {
    if (s->state != AAUDIO_STREAM_STATE_PAUSED && s->state != AAUDIO_STREAM_STATE_STOPPED)
        return AAUDIO_ERROR_INVALID_STATE;
    axrb_audio_flush(s->host);
    set_state(s, AAUDIO_STREAM_STATE_FLUSHED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_release(AAudioStream* s) {
    if (s->state != AAUDIO_STREAM_STATE_CLOSED) {
        join_callback_thread(s);
        axrb_audio_close(s->host);
        s->host = 0;
        set_state(s, AAUDIO_STREAM_STATE_CLOSING);
    }
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_close(AAudioStream* s) {
    AAudioStream_release(s);
    free(s->callback_buffer);
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->changed);
    free(s);
    return AAUDIO_OK;
}

aaudio_stream_state_t AAudioStream_getState(AAudioStream* s) { return s->state; }

aaudio_result_t AAudioStream_waitForStateChange(AAudioStream* s, aaudio_stream_state_t input,
                                                aaudio_stream_state_t* next, int64_t timeout_ns) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ns / 1000000000LL;
    deadline.tv_nsec += timeout_ns % 1000000000LL;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&s->lock);
    int r = 0;
    while (s->state == input && r != ETIMEDOUT) r = pthread_cond_timedwait(&s->changed, &s->lock, &deadline);
    aaudio_stream_state_t state = s->state;
    pthread_mutex_unlock(&s->lock);
    if (next) *next = state;
    return state == input ? AAUDIO_ERROR_TIMEOUT : AAUDIO_OK;
}

aaudio_result_t AAudioStream_read(AAudioStream* s, void* buffer, int32_t frames, int64_t timeout_ns) {
    return AAUDIO_ERROR_UNIMPLEMENTED;
}

aaudio_result_t AAudioStream_write(AAudioStream* s, const void* buffer, int32_t frames, int64_t timeout_ns) {
    if (s->params.data_callback) return AAUDIO_ERROR_INVALID_STATE;
    if (s->state != AAUDIO_STREAM_STATE_STARTED && s->state != AAUDIO_STREAM_STATE_OPEN &&
        s->state != AAUDIO_STREAM_STATE_PAUSED && s->state != AAUDIO_STREAM_STATE_FLUSHED &&
        s->state != AAUDIO_STREAM_STATE_STOPPED)
        return AAUDIO_ERROR_INVALID_STATE;
    const char* p = buffer;
    int32_t done = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (done < frames) {
        int64_t waited = 0;
        clock_gettime(CLOCK_MONOTONIC, &now);
        waited = (now.tv_sec - start.tv_sec) * 1000000000LL + (now.tv_nsec - start.tv_nsec);
        int64_t left = timeout_ns - waited;
        int32_t room = axrb_audio_wait(s->host, left > 0 ? left : 0);
        if (room < 0) return done ? done : AAUDIO_ERROR_DISCONNECTED;
        if (room == 0) {
            if (left <= 0) break;
            continue;
        }
        int32_t n = frames - done < room ? frames - done : room;
        n = axrb_audio_write(s->host, p + (size_t)done * s->frame_bytes, n);
        if (n < 0) return done ? done : n;
        done += n;
        s->frames_written += n;
    }
    return done;
}

aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream* s, int32_t frames) {
    if (frames <= 0 || frames > s->params.buffer_capacity) frames = s->params.buffer_capacity;
    s->buffer_size = frames;
    return frames;
}

int32_t AAudioStream_getBufferSizeInFrames(AAudioStream* s) { return s->buffer_size; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream* s) { return s->burst; }
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream* s) { return s->params.buffer_capacity; }
int32_t AAudioStream_getFramesPerDataCallback(AAudioStream* s) {
    return s->params.frames_per_callback > 0 ? s->params.frames_per_callback : AAUDIO_UNSPECIFIED;
}
int32_t AAudioStream_getXRunCount(AAudioStream* s) { return 0; }
int32_t AAudioStream_getSampleRate(AAudioStream* s) { return s->params.sample_rate; }
int32_t AAudioStream_getHardwareSampleRate(AAudioStream* s) { return s->params.sample_rate; }
int32_t AAudioStream_getChannelCount(AAudioStream* s) { return s->params.channel_count; }
int32_t AAudioStream_getHardwareChannelCount(AAudioStream* s) { return s->params.channel_count; }
int32_t AAudioStream_getSamplesPerFrame(AAudioStream* s) { return s->params.channel_count; }
int32_t AAudioStream_getDeviceId(AAudioStream* s) { return 2; }
aaudio_format_t AAudioStream_getFormat(AAudioStream* s) { return s->params.format; }
aaudio_format_t AAudioStream_getHardwareFormat(AAudioStream* s) { return AAUDIO_FORMAT_PCM_FLOAT; }
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream* s) { return s->params.sharing_mode; }
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream* s) { return s->params.performance_mode; }
aaudio_direction_t AAudioStream_getDirection(AAudioStream* s) { return s->params.direction; }
int64_t AAudioStream_getFramesWritten(AAudioStream* s) { return s->frames_written; }
int64_t AAudioStream_getFramesRead(AAudioStream* s) {
    int64_t frames = 0, t = 0;
    axrb_audio_position(s->host, &frames, &t);
    return frames;
}
int32_t AAudioStream_getSessionId(AAudioStream* s) { return s->params.session_id > 0 ? s->params.session_id : -1; }
int32_t AAudioStream_getUsage(AAudioStream* s) { return s->params.usage; }
int32_t AAudioStream_getContentType(AAudioStream* s) { return s->params.content_type; }
int32_t AAudioStream_getInputPreset(AAudioStream* s) { return s->params.input_preset; }
int32_t AAudioStream_getAllowedCapturePolicy(AAudioStream* s) { return s->params.capture_policy; }
int AAudioStream_isPrivacySensitive(AAudioStream* s) { return s->params.privacy_sensitive; }
uint32_t AAudioStream_getChannelMask(AAudioStream* s) { return s->params.channel_mask; }
int AAudioStream_isContentSpatialized(AAudioStream* s) { return 0; }
int32_t AAudioStream_getSpatializationBehavior(AAudioStream* s) { return 1; }

aaudio_result_t AAudioStream_getTimestamp(AAudioStream* s, clockid_t clockid, int64_t* frame_position,
                                          int64_t* time_ns) {
    if (s->state != AAUDIO_STREAM_STATE_STARTED) return AAUDIO_ERROR_INVALID_STATE;
    int64_t frames = 0, t = 0;
    if (axrb_audio_position(s->host, &frames, &t) < 0 || frames <= 0) return AAUDIO_ERROR_INVALID_STATE;
    if (frame_position) *frame_position = frames;
    if (time_ns) *time_ns = t;
    return AAUDIO_OK;
}
