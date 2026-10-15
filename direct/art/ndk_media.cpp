// AMediaExtractor and AMediaCodec for Direct mode. AOSP's versions need the
// media.extractor and media.codec services, which Direct mode does not run
// ("extractor service not running"). These call the host's Media Foundation
// backend (direct/src/media_codec.cpp) instead. Every other libmediandk export,
// AMediaFormat included, is forwarded to the matched AOSP library.
#include "../guest/axrb_guest.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" long axrb_mediandk(long op, long id, long a, long b, long c, long d);
AXRB_HOST_FUNCTION(axrb_mediandk, "art.mediandk");

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AXRB.Media", __VA_ARGS__)

namespace {
// Shared with direct/src/media_codec.cpp.
enum Op : long {
    kExtractorNew = 0, kExtractorSourceFd, kExtractorTrackCount, kExtractorTrack, kExtractorCsd,
    kExtractorSelect, kExtractorSeek, kExtractorSample, kExtractorRead, kExtractorAdvance,
    kExtractorDelete, kExtractorFileFormat,
    kCodecNew = 20, kCodecConfigure, kCodecStart, kCodecStop, kCodecFlush, kCodecDelete,
    kCodecInput, kCodecOutput, kCodecRelease, kCodecFormat, kCodecSurface,
    kWindowHandle = 40,
};
struct TrackInfo {
    char mime[64];
    int64_t duration_us;
    int32_t width, height, sample_rate, channels, max_input_size, frame_rate, bit_rate, is_adts;
    int32_t csd_count, reserved;
};
struct SampleInfo {
    int64_t time_us;
    int32_t track, flags, size, reserved;
};
struct CodecConfig {
    int32_t width, height, sample_rate, channels, is_adts, reserved;
    uint64_t csd[2];
    int32_t csd_size[2];
};
struct OutputInfo {
    int64_t time_us;
    uint64_t sequence;
    int32_t size, flags;
};
struct OutputFormat {
    char mime[64];
    int32_t width, height, stride, slice_height, color_format;
    int32_t crop_left, crop_top, crop_right, crop_bottom;
    int32_t sample_rate, channels, pcm_encoding;
};

long host(long op, long id = 0, long a = 0, long b = 0, long c = 0, long d = 0) {
    return axrb_mediandk(op, id, a, b, c, d);
}
media_status_t status(long value) { return value < 0 ? static_cast<media_status_t>(value) : AMEDIA_OK; }
}  // namespace

struct AMediaExtractor {
    long id;
};

struct AMediaCodec {
    long id;
    std::string mime;
    std::mutex lock;
    struct Input {
        std::vector<uint8_t> data;
        bool client = false;  // dequeued and not yet queued back
    };
    struct Output {
        std::vector<uint8_t> data;
        OutputInfo info{};
        bool client = false;
    };
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    size_t input_size = 0;
    long window = 0;
    bool started = false;
    bool input_ended = false;  // end of stream queued: no more input until flush
    // Asynchronous mode (AMediaCodec_setAsyncNotifyCallback).
    AMediaCodecOnAsyncNotifyCallback callback{};
    void* userdata = nullptr;
    std::thread worker;
    std::atomic<bool> running{false};
    std::vector<bool> offered;
};

extern "C" {

// --- AMediaExtractor -------------------------------------------------------------------------

AMediaExtractor* AMediaExtractor_new() {
    const long id = host(kExtractorNew);
    return id > 0 ? new AMediaExtractor{id} : nullptr;
}

media_status_t AMediaExtractor_delete(AMediaExtractor* x) {
    if (!x) return AMEDIA_ERROR_INVALID_OBJECT;
    host(kExtractorDelete, x->id);
    delete x;
    return AMEDIA_OK;
}

media_status_t AMediaExtractor_setDataSourceFd(AMediaExtractor* x, int fd, off64_t offset, off64_t length) {
    if (!x) return AMEDIA_ERROR_INVALID_OBJECT;
    return status(host(kExtractorSourceFd, x->id, fd, offset, length));
}

media_status_t AMediaExtractor_setDataSource(AMediaExtractor* x, const char* location) {
    if (!x || !location) return AMEDIA_ERROR_INVALID_PARAMETER;
    const char* path = strncmp(location, "file://", 7) == 0 ? location + 7 : location;
    if (path[0] != '/') {
        LOG("AMediaExtractor: only local files are supported, not %.64s", location);
        return AMEDIA_ERROR_UNSUPPORTED;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return AMEDIA_ERROR_IO;
    const media_status_t result = AMediaExtractor_setDataSourceFd(x, fd, 0, -1);
    close(fd);
    return result;
}

media_status_t AMediaExtractor_setDataSourceCustom(AMediaExtractor*, AMediaDataSource*) {
    LOG("AMediaExtractor: custom data sources are not supported");
    return AMEDIA_ERROR_UNSUPPORTED;
}

size_t AMediaExtractor_getTrackCount(AMediaExtractor* x) {
    const long count = x ? host(kExtractorTrackCount, x->id) : 0;
    return count > 0 ? static_cast<size_t>(count) : 0;
}

AMediaFormat* AMediaExtractor_getTrackFormat(AMediaExtractor* x, size_t index) {
    TrackInfo info{};
    if (!x || host(kExtractorTrack, x->id, static_cast<long>(index), reinterpret_cast<long>(&info)) < 0) return nullptr;
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", info.mime);
    if (info.duration_us > 0) AMediaFormat_setInt64(format, "durationUs", info.duration_us);
    if (info.width > 0) AMediaFormat_setInt32(format, "width", info.width);
    if (info.height > 0) AMediaFormat_setInt32(format, "height", info.height);
    if (info.frame_rate > 0) AMediaFormat_setInt32(format, "frame-rate", info.frame_rate);
    if (info.sample_rate > 0) AMediaFormat_setInt32(format, "sample-rate", info.sample_rate);
    if (info.channels > 0) AMediaFormat_setInt32(format, "channel-count", info.channels);
    if (info.max_input_size > 0) AMediaFormat_setInt32(format, "max-input-size", info.max_input_size);
    if (info.bit_rate > 0) AMediaFormat_setInt32(format, "bitrate", info.bit_rate);
    if (info.is_adts) AMediaFormat_setInt32(format, "is-adts", 1);
    for (int i = 0; i < info.csd_count; ++i) {
        const long size = host(kExtractorCsd, x->id, static_cast<long>(index), i, 0, 0);
        if (size <= 0) continue;
        std::vector<uint8_t> csd(static_cast<size_t>(size));
        host(kExtractorCsd, x->id, static_cast<long>(index), i, reinterpret_cast<long>(csd.data()), size);
        const char key[] = {'c', 's', 'd', '-', static_cast<char>('0' + i), 0};
        AMediaFormat_setBuffer(format, key, csd.data(), csd.size());
    }
    return format;
}

media_status_t AMediaExtractor_selectTrack(AMediaExtractor* x, size_t index) {
    return x ? status(host(kExtractorSelect, x->id, static_cast<long>(index), 1)) : AMEDIA_ERROR_INVALID_OBJECT;
}
media_status_t AMediaExtractor_unselectTrack(AMediaExtractor* x, size_t index) {
    return x ? status(host(kExtractorSelect, x->id, static_cast<long>(index), 0)) : AMEDIA_ERROR_INVALID_OBJECT;
}

ssize_t AMediaExtractor_readSampleData(AMediaExtractor* x, uint8_t* buffer, size_t capacity) {
    if (!x || !buffer) return -1;
    const long n = host(kExtractorRead, x->id, reinterpret_cast<long>(buffer), static_cast<long>(capacity));
    return n < 0 ? -1 : n;
}

static bool sample(AMediaExtractor* x, SampleInfo& info) {
    return x && host(kExtractorSample, x->id, reinterpret_cast<long>(&info)) == 0;
}
uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor* x) {
    SampleInfo info{};
    return sample(x, info) ? static_cast<uint32_t>(info.flags) : 0;
}
int AMediaExtractor_getSampleTrackIndex(AMediaExtractor* x) {
    SampleInfo info{};
    return sample(x, info) ? info.track : -1;
}
int64_t AMediaExtractor_getSampleTime(AMediaExtractor* x) {
    SampleInfo info{};
    return sample(x, info) ? info.time_us : -1;
}
ssize_t AMediaExtractor_getSampleSize(AMediaExtractor* x) {
    SampleInfo info{};
    return sample(x, info) ? info.size : -1;
}
bool AMediaExtractor_advance(AMediaExtractor* x) { return x && host(kExtractorAdvance, x->id) > 0; }

media_status_t AMediaExtractor_seekTo(AMediaExtractor* x, int64_t time_us, SeekMode) {
    return x ? status(host(kExtractorSeek, x->id, static_cast<long>(time_us))) : AMEDIA_ERROR_INVALID_OBJECT;
}

AMediaFormat* AMediaExtractor_getFileFormat(AMediaExtractor* x) {
    char mime[64]{};
    if (!x || host(kExtractorFileFormat, x->id, reinterpret_cast<long>(mime), sizeof mime) < 0) return nullptr;
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", mime);
    return format;
}

media_status_t AMediaExtractor_getSampleFormat(AMediaExtractor* x, AMediaFormat* format) {
    SampleInfo info{};
    if (!format) return AMEDIA_ERROR_INVALID_PARAMETER;
    if (!sample(x, info)) return AMEDIA_ERROR_END_OF_STREAM;
    AMediaFormat_clear(format);
    AMediaFormat_setInt64(format, "time-us", info.time_us);
    if (info.flags & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC) AMediaFormat_setInt32(format, "is-sync-frame", 1);
    return AMEDIA_OK;
}

int64_t AMediaExtractor_getCachedDuration(AMediaExtractor*) { return -1; }
PsshInfo* AMediaExtractor_getPsshInfo(AMediaExtractor*) { return nullptr; }
AMediaCodecCryptoInfo* AMediaExtractor_getSampleCryptoInfo(AMediaExtractor*) { return nullptr; }

// --- AMediaCodec ---------------------------------------------------------------------------

static const char* mime_for_name(const char* name) {
    static const struct { const char* part; const char* mime; } names[] = {
        {"avc", "video/avc"}, {"h264", "video/avc"}, {"hevc", "video/hevc"}, {"h265", "video/hevc"},
        {"vp9", "video/x-vnd.on2.vp9"}, {"vp8", "video/x-vnd.on2.vp8"}, {"av1", "video/av01"},
        {"mpeg4", "video/mp4v-es"}, {"aac", "audio/mp4a-latm"}, {"mp3", "audio/mpeg"},
        {"flac", "audio/flac"}, {"opus", "audio/opus"},
    };
    std::string lower(name ? name : "");
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (lower.find("encoder") != std::string::npos) return nullptr;
    for (auto& n : names)
        if (lower.find(n.part) != std::string::npos) return n.mime;
    return nullptr;
}

AMediaCodec* AMediaCodec_createDecoderByType(const char* mime) {
    if (!mime) return nullptr;
    const long id = host(kCodecNew, 0, reinterpret_cast<long>(mime));
    if (id <= 0) return nullptr;
    auto* codec = new AMediaCodec;
    codec->id = id;
    codec->mime = mime;
    return codec;
}
AMediaCodec* AMediaCodec_createCodecByName(const char* name) {
    const char* mime = mime_for_name(name);
    return mime ? AMediaCodec_createDecoderByType(mime) : nullptr;
}
AMediaCodec* AMediaCodec_createDecoderByTypeForClient(const char* mime, pid_t, uid_t) { return AMediaCodec_createDecoderByType(mime); }
AMediaCodec* AMediaCodec_createCodecByNameForClient(const char* name, pid_t, uid_t) { return AMediaCodec_createCodecByName(name); }
AMediaCodec* AMediaCodec_createEncoderByType(const char* mime) {
    LOG("AMediaCodec: encoders are not supported (%s)", mime ? mime : "?");
    return nullptr;
}
AMediaCodec* AMediaCodec_createEncoderByTypeForClient(const char* mime, pid_t, uid_t) { return AMediaCodec_createEncoderByType(mime); }

static void stop_worker(AMediaCodec* codec) {
    codec->running = false;
    if (codec->worker.joinable() && codec->worker.get_id() != std::this_thread::get_id()) codec->worker.join();
    else if (codec->worker.joinable()) codec->worker.detach();
}

media_status_t AMediaCodec_delete(AMediaCodec* codec) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    stop_worker(codec);
    host(kCodecDelete, codec->id);
    delete codec;
    return AMEDIA_OK;
}

media_status_t AMediaCodec_configure(AMediaCodec* codec, const AMediaFormat* format, ANativeWindow* window,
                                     AMediaCrypto* crypto, uint32_t flags) {
    if (!codec || !format) return AMEDIA_ERROR_INVALID_PARAMETER;
    if (flags & AMEDIACODEC_CONFIGURE_FLAG_ENCODE) return AMEDIA_ERROR_UNSUPPORTED;
    if (crypto) {
        LOG("AMediaCodec: encrypted media is not supported");
        return AMEDIA_ERROR_UNSUPPORTED;
    }
    auto* f = const_cast<AMediaFormat*>(format);
    CodecConfig config{};
    AMediaFormat_getInt32(f, "width", &config.width);
    AMediaFormat_getInt32(f, "height", &config.height);
    AMediaFormat_getInt32(f, "sample-rate", &config.sample_rate);
    AMediaFormat_getInt32(f, "channel-count", &config.channels);
    AMediaFormat_getInt32(f, "is-adts", &config.is_adts);
    for (int i = 0; i < 2; ++i) {
        void* data = nullptr;
        size_t size = 0;
        const char key[] = {'c', 's', 'd', '-', static_cast<char>('0' + i), 0};
        if (AMediaFormat_getBuffer(f, key, &data, &size) && data && size) {
            config.csd[i] = reinterpret_cast<uint64_t>(data);
            config.csd_size[i] = static_cast<int32_t>(size);
        }
    }
    int32_t max_input = 0;
    AMediaFormat_getInt32(f, "max-input-size", &max_input);
    const bool video = codec->mime.compare(0, 6, "video/") == 0;
    const size_t fallback = video ? std::max<size_t>(size_t(config.width) * config.height * 3 / 2, 1 << 20) : 64 * 1024;
    codec->input_size = std::max<size_t>(max_input > 0 ? size_t(max_input) : 0, fallback);
    codec->window = window ? host(kWindowHandle, 0, reinterpret_cast<long>(window)) : 0;
    if (window && codec->window <= 0) {
        LOG("AMediaCodec: output Surface is not an AXRB host surface; frames are decoded but not shown");
        codec->window = 0;
    }
    std::lock_guard<std::mutex> guard(codec->lock);
    codec->inputs.assign(4, {});
    codec->outputs.assign(video ? 8 : 4, {});
    return status(host(kCodecConfigure, codec->id, reinterpret_cast<long>(&config), codec->window > 0 ? codec->window : 0));
}

media_status_t AMediaCodec_setOutputSurface(AMediaCodec* codec, ANativeWindow* window) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    const long handle = window ? host(kWindowHandle, 0, reinterpret_cast<long>(window)) : 0;
    codec->window = handle > 0 ? handle : 0;
    return status(host(kCodecSurface, codec->id, codec->window));
}

static void reset_buffers(AMediaCodec* codec) {
    std::lock_guard<std::mutex> guard(codec->lock);
    for (auto& in : codec->inputs) in.client = false;
    for (auto& out : codec->outputs) out.client = false;
    codec->offered.assign(codec->inputs.size(), false);
    codec->input_ended = false;
}

static void async_loop(AMediaCodec* codec);

media_status_t AMediaCodec_start(AMediaCodec* codec) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    const media_status_t result = status(host(kCodecStart, codec->id));
    if (result != AMEDIA_OK) return result;
    reset_buffers(codec);
    codec->started = true;
    if (codec->callback.onAsyncInputAvailable && !codec->running) {
        stop_worker(codec);
        codec->running = true;
        codec->worker = std::thread(async_loop, codec);
    }
    return AMEDIA_OK;
}

media_status_t AMediaCodec_stop(AMediaCodec* codec) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    stop_worker(codec);
    codec->started = false;
    host(kCodecStop, codec->id);
    reset_buffers(codec);
    return AMEDIA_OK;
}

media_status_t AMediaCodec_flush(AMediaCodec* codec) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    // In asynchronous mode, a flushed codec resumes at the next start().
    stop_worker(codec);
    host(kCodecFlush, codec->id);
    reset_buffers(codec);
    return AMEDIA_OK;
}

ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec* codec, int64_t) {
    if (!codec || !codec->started) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    std::lock_guard<std::mutex> guard(codec->lock);
    for (size_t i = 0; i < codec->inputs.size(); ++i) {
        if (codec->inputs[i].client) continue;
        codec->inputs[i].client = true;
        if (codec->inputs[i].data.size() < codec->input_size) codec->inputs[i].data.resize(codec->input_size);
        return static_cast<ssize_t>(i);
    }
    return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}

uint8_t* AMediaCodec_getInputBuffer(AMediaCodec* codec, size_t index, size_t* size) {
    if (!codec) return nullptr;
    std::lock_guard<std::mutex> guard(codec->lock);
    if (index >= codec->inputs.size()) return nullptr;
    auto& in = codec->inputs[index];
    if (in.data.size() < codec->input_size) in.data.resize(codec->input_size);
    if (size) *size = in.data.size();
    return in.data.data();
}

media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* codec, size_t index, off_t offset, size_t size, uint64_t time,
                                            uint32_t flags) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    uint8_t* data;
    {
        std::lock_guard<std::mutex> guard(codec->lock);
        if (index >= codec->inputs.size() || offset < 0 || size_t(offset) + size > codec->inputs[index].data.size())
            return AMEDIA_ERROR_INVALID_PARAMETER;
        data = codec->inputs[index].data.data() + offset;
    }
    const media_status_t result = status(host(kCodecInput, codec->id, reinterpret_cast<long>(data), static_cast<long>(size),
                                              static_cast<long>(time), static_cast<long>(flags)));
    std::lock_guard<std::mutex> guard(codec->lock);
    codec->inputs[index].client = false;
    if (index < codec->offered.size()) codec->offered[index] = false;
    if (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) codec->input_ended = true;
    return result;
}

media_status_t AMediaCodec_queueSecureInputBuffer(AMediaCodec*, size_t, off_t, AMediaCodecCryptoInfo*, uint64_t, uint32_t) {
    return AMEDIA_ERROR_UNSUPPORTED;
}

// One output step: a buffer index, OUTPUT_FORMAT_CHANGED or TRY_AGAIN_LATER.
static ssize_t next_output(AMediaCodec* codec, AMediaCodecBufferInfo* info, int64_t timeout_us) {
    if (!codec || !codec->started) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    size_t index = 0;
    {
        std::lock_guard<std::mutex> guard(codec->lock);
        while (index < codec->outputs.size() && codec->outputs[index].client) ++index;
        if (index == codec->outputs.size()) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
        codec->outputs[index].client = true;  // reserved while the host fills it
    }
    auto& out = codec->outputs[index];
    long result;
    for (;;) {
        OutputInfo got{};
        const bool surface = codec->window > 0;
        result = host(kCodecOutput, codec->id, reinterpret_cast<long>(&got), surface ? 0 : reinterpret_cast<long>(out.data.data()),
                      surface ? 0 : static_cast<long>(out.data.size()), static_cast<long>(timeout_us));
        if (result == -3) {
            out.data.resize(static_cast<size_t>(got.size));
            continue;
        }
        out.info = got;
        break;
    }
    if (result != 0) {
        std::lock_guard<std::mutex> guard(codec->lock);
        out.client = false;
        if (result == 1) return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
        return result == -1 ? AMEDIACODEC_INFO_TRY_AGAIN_LATER : result;
    }
    if (info) {
        info->offset = 0;
        info->size = out.info.size;
        info->presentationTimeUs = out.info.time_us;
        info->flags = static_cast<uint32_t>(out.info.flags);
    }
    return static_cast<ssize_t>(index);
}

ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec* codec, AMediaCodecBufferInfo* info, int64_t timeout_us) {
    return next_output(codec, info, timeout_us);
}

uint8_t* AMediaCodec_getOutputBuffer(AMediaCodec* codec, size_t index, size_t* size) {
    if (!codec) return nullptr;
    std::lock_guard<std::mutex> guard(codec->lock);
    if (index >= codec->outputs.size() || codec->window > 0) return nullptr;
    auto& out = codec->outputs[index];
    if (size) *size = out.data.size();
    return out.data.data();
}

media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec* codec, size_t index, bool render) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    uint64_t sequence;
    {
        std::lock_guard<std::mutex> guard(codec->lock);
        if (index >= codec->outputs.size() || !codec->outputs[index].client) return AMEDIA_ERROR_INVALID_PARAMETER;
        sequence = codec->outputs[index].info.sequence;
        codec->outputs[index].client = false;
    }
    host(kCodecRelease, codec->id, static_cast<long>(sequence), render ? 1 : 0);
    return AMEDIA_OK;
}

media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec* codec, size_t index, int64_t) {
    return AMediaCodec_releaseOutputBuffer(codec, index, true);
}

AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec* codec) {
    OutputFormat out{};
    if (!codec || host(kCodecFormat, codec->id, reinterpret_cast<long>(&out)) < 0) return nullptr;
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", out.mime);
    if (out.width > 0) {
        AMediaFormat_setInt32(format, "width", out.width);
        AMediaFormat_setInt32(format, "height", out.height);
        AMediaFormat_setInt32(format, "stride", out.stride);
        AMediaFormat_setInt32(format, "slice-height", out.slice_height);
        AMediaFormat_setInt32(format, "color-format", out.color_format);
        AMediaFormat_setInt32(format, "crop-left", out.crop_left);
        AMediaFormat_setInt32(format, "crop-top", out.crop_top);
        AMediaFormat_setInt32(format, "crop-right", out.crop_right);
        AMediaFormat_setInt32(format, "crop-bottom", out.crop_bottom);
    }
    if (out.sample_rate > 0) {
        AMediaFormat_setInt32(format, "sample-rate", out.sample_rate);
        AMediaFormat_setInt32(format, "channel-count", out.channels);
        AMediaFormat_setInt32(format, "pcm-encoding", out.pcm_encoding);
    }
    return format;
}

AMediaFormat* AMediaCodec_getBufferFormat(AMediaCodec* codec, size_t) { return AMediaCodec_getOutputFormat(codec); }

AMediaFormat* AMediaCodec_getInputFormat(AMediaCodec* codec) {
    if (!codec) return nullptr;
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", codec->mime.c_str());
    return format;
}

media_status_t AMediaCodec_getName(AMediaCodec* codec, char** name) {
    if (!codec || !name) return AMEDIA_ERROR_INVALID_PARAMETER;
    std::string value = "c2.axrb." + codec->mime.substr(codec->mime.find('/') + 1) + ".decoder";
    *name = strdup(value.c_str());
    return AMEDIA_OK;
}
void AMediaCodec_releaseName(AMediaCodec*, char* name) { free(name); }

media_status_t AMediaCodec_setParameters(AMediaCodec* codec, const AMediaFormat*) {
    return codec ? AMEDIA_OK : AMEDIA_ERROR_INVALID_OBJECT;
}
media_status_t AMediaCodec_releaseCrypto(AMediaCodec* codec) { return codec ? AMEDIA_OK : AMEDIA_ERROR_INVALID_OBJECT; }
media_status_t AMediaCodec_signalEndOfInputStream(AMediaCodec*) { return AMEDIA_ERROR_UNSUPPORTED; }
media_status_t AMediaCodec_createInputSurface(AMediaCodec*, ANativeWindow**) { return AMEDIA_ERROR_UNSUPPORTED; }
media_status_t AMediaCodec_createPersistentInputSurface(ANativeWindow**) { return AMEDIA_ERROR_UNSUPPORTED; }
media_status_t AMediaCodec_setInputSurface(AMediaCodec*, ANativeWindow*) { return AMEDIA_ERROR_UNSUPPORTED; }
media_status_t AMediaCodec_setOnFrameRenderedCallback(AMediaCodec* codec, AMediaCodecOnFrameRendered, void*) {
    return codec ? AMEDIA_OK : AMEDIA_ERROR_INVALID_OBJECT;
}

media_status_t AMediaCodec_setAsyncNotifyCallback(AMediaCodec* codec, AMediaCodecOnAsyncNotifyCallback callback, void* userdata) {
    if (!codec) return AMEDIA_ERROR_INVALID_OBJECT;
    if (codec->started) return AMEDIA_ERROR_INVALID_OPERATION;
    codec->callback = callback;
    codec->userdata = userdata;
    return AMEDIA_OK;
}

}  // extern "C"

// Asynchronous mode: offers free input buffers and delivers decoded output on
// a codec thread, like MediaCodec's callback looper.
static void async_loop(AMediaCodec* codec) {
    pthread_setname_np(pthread_self(), "AXRB codec");
    while (codec->running) {
        bool busy = false;
        // Each free input buffer is offered once per round, and none after
        // the end of stream, so output keeps flowing between offers.
        for (size_t next = 0;; ++next) {
            size_t index;
            {
                std::lock_guard<std::mutex> guard(codec->lock);
                if (codec->input_ended) break;
                index = next;
                while (index < codec->inputs.size() && (codec->inputs[index].client || codec->offered[index])) ++index;
                if (index == codec->inputs.size()) break;
                next = index;
                codec->offered[index] = true;
                codec->inputs[index].client = true;
                if (codec->inputs[index].data.size() < codec->input_size) codec->inputs[index].data.resize(codec->input_size);
            }
            codec->callback.onAsyncInputAvailable(codec, codec->userdata, static_cast<int32_t>(index));
            busy = true;
            if (!codec->running) return;
        }
        AMediaCodecBufferInfo info{};
        const ssize_t index = next_output(codec, &info, 0);
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
            if (codec->callback.onAsyncFormatChanged) codec->callback.onAsyncFormatChanged(codec, codec->userdata, format);
            AMediaFormat_delete(format);
            busy = true;
        } else if (index >= 0) {
            if (codec->callback.onAsyncOutputAvailable)
                codec->callback.onAsyncOutputAvailable(codec, codec->userdata, static_cast<int32_t>(index), &info);
            else
                AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
            busy = true;
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            if (codec->callback.onAsyncError)
                codec->callback.onAsyncError(codec, codec->userdata, static_cast<media_status_t>(index), 0, "decode failed");
            return;
        }
        if (!busy) {
            timespec pause{0, 2000000};
            nanosleep(&pause, nullptr);
        }
    }
}
