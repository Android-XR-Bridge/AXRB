// AMediaExtractor + AMediaCodec on an MP4 asset (H.264 video, AAC audio),
// opened the way Unity's video player does: the APK's asset descriptor.
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char report[8192];
static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t used = strlen(report);
    vsnprintf(report + used, sizeof report - used, format, args);
    va_end(args);
}

struct Result {
    int frames, width, height, rate, channels, varied;
    long bytes;
};

static void inspect(const uint8_t* data, int size, struct Result* r) {
    if (size <= 0) return;
    uint8_t low = data[0], high = data[0];
    for (int i = 0; i < size; i += 7) {
        if (data[i] < low) low = data[i];
        if (data[i] > high) high = data[i];
    }
    if (high - low > 32) r->varied++;
}

static void output_format(AMediaCodec* codec, struct Result* r) {
    AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
    if (!format) return;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &r->width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &r->height);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &r->rate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &r->channels);
    AMediaFormat_delete(format);
}

// Synchronous decoding of one track to its end.
static int decode(AMediaExtractor* x, size_t track, struct Result* r) {
    AMediaFormat* format = AMediaExtractor_getTrackFormat(x, track);
    const char* mime = NULL;
    if (!format || !AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) return -1;
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) { say("no decoder for %s; ", mime); return -2; }
    if (AMediaCodec_configure(codec, format, NULL, NULL, 0) != AMEDIA_OK || AMediaCodec_start(codec) != AMEDIA_OK) return -3;
    AMediaExtractor_selectTrack(x, track);
    AMediaExtractor_seekTo(x, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    int input_done = 0, output_done = 0;
    for (int guard = 0; !output_done && guard < 100000; ++guard) {
        if (!input_done) {
            ssize_t in = AMediaCodec_dequeueInputBuffer(codec, 1000);
            if (in >= 0) {
                size_t capacity = 0;
                uint8_t* buffer = AMediaCodec_getInputBuffer(codec, (size_t)in, &capacity);
                ssize_t n = AMediaExtractor_readSampleData(x, buffer, capacity);
                if (n < 0) {
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    input_done = 1;
                } else {
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, (size_t)n, (uint64_t)AMediaExtractor_getSampleTime(x), 0);
                    AMediaExtractor_advance(x);
                }
            }
        }
        AMediaCodecBufferInfo info;
        ssize_t out = AMediaCodec_dequeueOutputBuffer(codec, &info, 1000);
        if (out >= 0) {
            size_t size = 0;
            uint8_t* buffer = AMediaCodec_getOutputBuffer(codec, (size_t)out, &size);
            if (info.size > 0 && buffer) {
                r->frames++;
                r->bytes += info.size;
                inspect(buffer + info.offset, info.size, r);
            }
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) output_done = 1;
            AMediaCodec_releaseOutputBuffer(codec, (size_t)out, false);
        } else if (out == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            output_format(codec, r);
        }
    }
    AMediaExtractor_unselectTrack(x, track);
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    return output_done ? 0 : -4;
}

// Asynchronous decoding (callbacks on the codec's thread).
struct Async {
    AMediaExtractor* x;
    struct Result r;
    pthread_mutex_t lock;
    int input_done, output_done;
};
static void on_input(AMediaCodec* codec, void* user, int32_t index) {
    struct Async* a = user;
    pthread_mutex_lock(&a->lock);
    size_t capacity = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(codec, (size_t)index, &capacity);
    ssize_t n = a->input_done ? -1 : AMediaExtractor_readSampleData(a->x, buffer, capacity);
    if (n < 0) {
        AMediaCodec_queueInputBuffer(codec, (size_t)index, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        a->input_done = 1;
    } else {
        AMediaCodec_queueInputBuffer(codec, (size_t)index, 0, (size_t)n, (uint64_t)AMediaExtractor_getSampleTime(a->x), 0);
        AMediaExtractor_advance(a->x);
    }
    pthread_mutex_unlock(&a->lock);
}
static void on_output(AMediaCodec* codec, void* user, int32_t index, AMediaCodecBufferInfo* info) {
    struct Async* a = user;
    size_t size = 0;
    uint8_t* buffer = AMediaCodec_getOutputBuffer(codec, (size_t)index, &size);
    pthread_mutex_lock(&a->lock);
    if (info->size > 0 && buffer) {
        a->r.frames++;
        a->r.bytes += info->size;
        inspect(buffer + info->offset, info->size, &a->r);
    }
    if (info->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) a->output_done = 1;
    pthread_mutex_unlock(&a->lock);
    AMediaCodec_releaseOutputBuffer(codec, (size_t)index, false);
}
static void on_format(AMediaCodec* codec, void* user, AMediaFormat* format) {
    struct Async* a = user;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &a->r.rate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &a->r.channels);
}
static void on_error(AMediaCodec* codec, void* user, media_status_t error, int32_t code, const char* detail) {
    say("async error %d %s; ", error, detail);
    ((struct Async*)user)->output_done = -1;
}

static int decode_async(AMediaExtractor* x, size_t track, struct Result* r) {
    AMediaFormat* format = AMediaExtractor_getTrackFormat(x, track);
    const char* mime = NULL;
    AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) return -2;
    static struct Async a;
    memset(&a, 0, sizeof a);
    a.x = x;
    pthread_mutex_init(&a.lock, NULL);
    AMediaCodecOnAsyncNotifyCallback callback = {on_input, on_output, on_format, on_error};
    if (AMediaCodec_setAsyncNotifyCallback(codec, callback, &a) != AMEDIA_OK) return -3;
    if (AMediaCodec_configure(codec, format, NULL, NULL, 0) != AMEDIA_OK) return -4;
    AMediaExtractor_selectTrack(x, track);
    AMediaExtractor_seekTo(x, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    if (AMediaCodec_start(codec) != AMEDIA_OK) return -5;
    for (int i = 0; i < 1000 && !a.output_done; ++i) {
        struct timespec pause = {0, 10000000};
        nanosleep(&pause, NULL);
    }
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_unselectTrack(x, track);
    AMediaFormat_delete(format);
    *r = a.r;
    return a.output_done == 1 ? 0 : -6;
}

JNIEXPORT jstring JNICALL Java_com_axrb_mediaprobe_MediaCodecProbe_run(JNIEnv* env, jclass cls, jobject assets) {
    report[0] = 0;
    AAssetManager* manager = AAssetManager_fromJava(env, assets);
    AAsset* asset = manager ? AAssetManager_open(manager, "probe.mp4", AASSET_MODE_RANDOM) : NULL;
    off_t start = 0, length = 0;
    int fd = asset ? AAsset_openFileDescriptor(asset, &start, &length) : -1;
    if (fd < 0) return (*env)->NewStringUTF(env, "MEDIA_CODEC_PROBE_FAIL no asset descriptor");
    AMediaExtractor* x = AMediaExtractor_new();
    media_status_t opened = AMediaExtractor_setDataSourceFd(x, fd, start, length);
    close(fd);
    AAsset_close(asset);
    if (opened != AMEDIA_OK) {
        say("MEDIA_CODEC_PROBE_FAIL setDataSourceFd %d", opened);
        return (*env)->NewStringUTF(env, report);
    }
    size_t tracks = AMediaExtractor_getTrackCount(x);
    say("tracks=%zu; ", tracks);
    int ok = tracks == 2;
    for (size_t t = 0; t < tracks; ++t) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(x, t);
        const char* mime = "?";
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        int video = strncmp(mime, "video/", 6) == 0;
        void* csd = NULL;
        size_t csd_size = 0;
        AMediaFormat_getBuffer(format, "csd-0", &csd, &csd_size);
        say("%s csd0=%zu; ", mime, csd_size);
        struct Result r = {0};
        int status = decode(x, t, &r);
        say("%s sync: status=%d frames=%d bytes=%ld size=%dx%d rate=%d ch=%d varied=%d; ", mime, status, r.frames, r.bytes,
            r.width, r.height, r.rate, r.channels, r.varied);
        if (video) {
            ok &= status == 0 && r.frames == 30 && r.width >= 320 && r.height >= 240 && r.varied >= 25 && csd_size > 0;
        } else {
            ok &= status == 0 && r.rate == 44100 && r.channels == 2 && r.bytes > 160000 && r.bytes < 200000 && r.varied > 10;
            struct Result async = {0};
            status = decode_async(x, t, &async);
            say("%s async: status=%d frames=%d bytes=%ld rate=%d; ", mime, status, async.frames, async.bytes, async.rate);
            ok &= status == 0 && async.bytes == r.bytes && async.rate == 44100;
        }
        AMediaFormat_delete(format);
    }
    AMediaExtractor_delete(x);
    say(ok ? "MEDIA_CODEC_PROBE_PASS" : "MEDIA_CODEC_PROBE_FAIL");
    return (*env)->NewStringUTF(env, report);
}
