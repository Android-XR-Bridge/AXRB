// AMediaExtractor and AMediaCodec (libmediandk) on Media Foundation. AOSP's
// implementations need the media.extractor and media.codec services, which
// Direct mode does not run. The guest proxy (direct/art/ndk_media.cpp) calls
// "art.mediandk" instead: a Source Reader demuxes compressed samples and
// decoder MFTs turn them into PCM, NV12 buffers or Surface frames.

#include "media.h"
#include "cpu.h"
#include "kernel.h"
#include "linux.h"
#include "log.h"
#include "memory.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <shlwapi.h>
#include <wmcodecdsp.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace axrb {
namespace {

// Shared with direct/art/ndk_media.cpp; plain fixed-size layouts.
enum Op : uint64_t {
    kExtractorNew = 0, kExtractorSourceFd, kExtractorTrackCount, kExtractorTrack, kExtractorCsd,
    kExtractorSelect, kExtractorSeek, kExtractorSample, kExtractorRead, kExtractorAdvance,
    kExtractorDelete, kExtractorFileFormat,
    kCodecNew = 20, kCodecConfigure, kCodecStart, kCodecStop, kCodecFlush, kCodecDelete,
    kCodecInput, kCodecOutput, kCodecRelease, kCodecFormat, kCodecSurface,
    kWindowHandle = 40, kWindowRegister,
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
// media_status_t values and buffer flags of the NDK.
constexpr int64_t kErrorUnknown = -10000, kErrorMalformed = -10001, kErrorUnsupported = -10002,
                  kErrorInvalidOperation = -10006;
constexpr int32_t kFlagCodecConfig = 2, kFlagEndOfStream = 4;
constexpr int32_t kColorYuv420SemiPlanar = 21;

template <typename T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

bool media_foundation() {
    static const bool ready = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }();
    return ready;
}

int64_t sys(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    GuestThread* t = current_thread();
    if (!t) return -linux_abi::EBADF_;
    return do_syscall(*t, nr, a0, a1, a2, a3, 0, 0);
}

bool guest_writable(uint64_t address, uint64_t size) {
    return address && address_space().check(address, size, linux_abi::PROT_WRITE_) && address_space().populate(address, size);
}
bool guest_readable(uint64_t address, uint64_t size) {
    return address && address_space().check(address, size, linux_abi::PROT_READ_) && address_space().populate(address, size);
}

std::vector<uint8_t> buffer_bytes(IMFSample* sample) {
    std::vector<uint8_t> bytes;
    Com<IMFMediaBuffer> buffer;
    if (!sample || FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return bytes;
    BYTE* data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) return bytes;
    bytes.assign(data, data + length);
    buffer->Unlock();
    return bytes;
}

std::vector<uint8_t> blob(IMFAttributes* attributes, const GUID& key) {
    UINT32 size = 0;
    std::vector<uint8_t> bytes;
    if (FAILED(attributes->GetBlobSize(key, &size)) || !size) return bytes;
    bytes.resize(size);
    if (FAILED(attributes->GetBlob(key, bytes.data(), size, nullptr))) bytes.clear();
    return bytes;
}

// --- H.264/HEVC bitstream helpers ---------------------------------------------------------

const uint8_t kStartCode[4] = {0, 0, 0, 1};

bool has_start_code(const std::vector<uint8_t>& b) {
    return (b.size() >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1) ||
           (b.size() >= 3 && b[0] == 0 && b[1] == 0 && b[2] == 1);
}

// 4-byte length-prefixed NAL units (MP4 sample layout) to Annex B, which
// Android extractors produce and decoders accept. Annex B input is unchanged.
std::vector<uint8_t> to_annex_b(std::vector<uint8_t> bytes) {
    if (has_start_code(bytes)) return bytes;
    std::vector<uint8_t> out;
    size_t at = 0;
    while (at + 4 <= bytes.size()) {
        const uint32_t n = (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
        at += 4;
        if (n == 0 || n > bytes.size() - at) return bytes;
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), bytes.begin() + at, bytes.begin() + at + n);
        at += n;
    }
    return at == bytes.size() ? out : bytes;
}

// Annex B NAL units of a byte string, without start codes.
std::vector<std::pair<size_t, size_t>> nal_units(const std::vector<uint8_t>& b) {
    std::vector<std::pair<size_t, size_t>> units;
    size_t i = 0, begin = std::string::npos;
    while (i + 3 <= b.size()) {
        if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
            if (begin != std::string::npos) {
                size_t end = i;
                while (end > begin && b[end - 1] == 0) --end;
                units.push_back({begin, end - begin});
            }
            i += 3;
            begin = i;
        } else {
            ++i;
        }
    }
    if (begin != std::string::npos && begin < b.size()) units.push_back({begin, b.size() - begin});
    return units;
}

// avcC / hvcC configuration records to Annex B parameter sets.
std::vector<uint8_t> avcc_to_annex_b(const std::vector<uint8_t>& c) {
    std::vector<uint8_t> out;
    if (c.size() < 7 || c[0] != 1) return out;
    size_t at = 5;
    for (int list = 0; list < 2 && at < c.size(); ++list) {
        int count = list == 0 ? (c[at] & 0x1f) : c[at];
        ++at;
        for (int i = 0; i < count && at + 2 <= c.size(); ++i) {
            const size_t n = (size_t(c[at]) << 8) | c[at + 1];
            at += 2;
            if (at + n > c.size()) return {};
            out.insert(out.end(), kStartCode, kStartCode + 4);
            out.insert(out.end(), c.begin() + at, c.begin() + at + n);
            at += n;
        }
    }
    return out;
}
std::vector<uint8_t> hvcc_to_annex_b(const std::vector<uint8_t>& c) {
    std::vector<uint8_t> out;
    if (c.size() < 23 || c[0] != 1) return out;
    size_t at = 22;
    const int arrays = c[at++];
    for (int a = 0; a < arrays && at + 3 <= c.size(); ++a) {
        at += 1;
        const int count = (c[at] << 8) | c[at + 1];
        at += 2;
        for (int i = 0; i < count && at + 2 <= c.size(); ++i) {
            const size_t n = (size_t(c[at]) << 8) | c[at + 1];
            at += 2;
            if (at + n > c.size()) return {};
            out.insert(out.end(), kStartCode, kStartCode + 4);
            out.insert(out.end(), c.begin() + at, c.begin() + at + n);
            at += n;
        }
    }
    return out;
}

// MPEG-4 AudioSpecificConfig for AAC-LC.
std::vector<uint8_t> aac_config(uint32_t rate, uint32_t channels) {
    static const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    uint8_t index = 4;
    for (uint8_t i = 0; i < 13; ++i)
        if (rates[i] == rate) index = i;
    return {static_cast<uint8_t>((2 << 3) | (index >> 1)), static_cast<uint8_t>(((index & 1) << 7) | ((channels & 0xf) << 3))};
}

struct Mime {
    const GUID* subtype;
    const char* mime;
    bool video;
};
const Mime kMimes[] = {
    {&MFVideoFormat_H264, "video/avc", true},        {&MFVideoFormat_HEVC, "video/hevc", true},
    {&MFVideoFormat_VP90, "video/x-vnd.on2.vp9", true}, {&MFVideoFormat_VP80, "video/x-vnd.on2.vp8", true},
    {&MFVideoFormat_AV1, "video/av01", true},        {&MFVideoFormat_MP4V, "video/mp4v-es", true},
    {&MFVideoFormat_M4S2, "video/mp4v-es", true},    {&MFAudioFormat_AAC, "audio/mp4a-latm", false},
    {&MFAudioFormat_MP3, "audio/mpeg", false},       {&MFAudioFormat_MPEG, "audio/mpeg", false},
    {&MFAudioFormat_FLAC, "audio/flac", false},      {&MFAudioFormat_Opus, "audio/opus", false},
    {&MFAudioFormat_ALAC, "audio/alac", false},      {&MFAudioFormat_PCM, "audio/raw", false},
};
const Mime* mime_for(const GUID& subtype) {
    for (auto& m : kMimes)
        if (*m.subtype == subtype) return &m;
    return nullptr;
}
const Mime* mime_named(const std::string& name) {
    for (auto& m : kMimes)
        if (name == m.mime && *m.subtype != MFVideoFormat_M4S2 && *m.subtype != MFAudioFormat_MPEG) return &m;
    return nullptr;
}

void copy_string(char (&out)[64], const std::string& value) {
    std::memset(out, 0, sizeof out);
    std::memcpy(out, value.data(), std::min(value.size(), sizeof out - 1));
}

// --- Extractor ------------------------------------------------------------------------------

class Extractor {
public:
    std::mutex calls;  // held by the dispatcher for each call
    struct Track {
        DWORD stream;
        TrackInfo info{};
        std::vector<std::vector<uint8_t>> csd;
        bool selected = false, ended = false, annex_b = false;
    };

    int64_t open(std::vector<uint8_t> bytes) {
        if (!media_foundation()) return kErrorUnknown;
        source_ = std::move(bytes);
        Com<IStream> stream;
        stream.p = SHCreateMemStream(source_.data(), static_cast<UINT>(source_.size()));
        if (!stream) return kErrorUnknown;
        Com<IMFByteStream> byte_stream;
        if (FAILED(MFCreateMFByteStreamOnStream(stream.p, byte_stream.put()))) return kErrorUnknown;
        // The scheme resolver picks a byte-stream handler by content type.
        const wchar_t* type = sniff();
        Com<IMFAttributes> stream_attributes;
        if (type && SUCCEEDED(byte_stream->QueryInterface(IID_PPV_ARGS(stream_attributes.put()))))
            stream_attributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, type);
        HRESULT hr = MFCreateSourceReaderFromByteStream(byte_stream.p, nullptr, reader_.put());
        if (FAILED(hr)) {
            LOGW("media", "AMediaExtractor: unsupported container (0x%08lx)", static_cast<unsigned long>(hr));
            return kErrorUnsupported;
        }
        int64_t duration_us = 0;
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)) &&
            value.vt == VT_UI8)
            duration_us = static_cast<int64_t>(value.uhVal.QuadPart / 10);
        PropVariantClear(&value);
        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> native;
            hr = reader_->GetNativeMediaType(i, 0, native.put());
            if (hr == MF_E_INVALIDSTREAMNUMBER) break;
            reader_->SetStreamSelection(i, FALSE);
            if (FAILED(hr)) continue;
            Track track;
            track.stream = i;
            if (!describe(native.p, track)) continue;
            track.info.duration_us = duration_us;
            tracks_.push_back(std::move(track));
        }
        LOGI("media", "AMediaExtractor: %s, %zu track(s), %lld ms", type ? "container" : "unknown container",
             tracks_.size(), static_cast<long long>(duration_us / 1000));
        for (auto& t : tracks_)
            LOGI("media", "AMediaExtractor: track %s %dx%d %d Hz %d ch", t.info.mime, t.info.width, t.info.height,
                 t.info.sample_rate, t.info.channels);
        return tracks_.empty() ? kErrorUnsupported : 0;
    }

    size_t track_count() const { return tracks_.size(); }
    const Track* track(uint64_t index) const { return index < tracks_.size() ? &tracks_[index] : nullptr; }
    std::string file_format() const { return format_; }

    int64_t select(uint64_t index, bool on) {
        if (index >= tracks_.size()) return kErrorInvalidOperation;
        auto& t = tracks_[index];
        if (t.selected == on) return 0;
        t.selected = on;
        t.ended = false;
        reader_->SetStreamSelection(t.stream, on);
        if (!on && has_sample_ && sample_track_ == static_cast<int32_t>(index)) has_sample_ = false;
        if (on && !has_sample_ && !ended_) loaded_ = false;
        if (on) ended_ = false;
        return 0;
    }

    int64_t seek(int64_t time_us) {
        PROPVARIANT at;
        PropVariantInit(&at);
        at.vt = VT_I8;
        at.hVal.QuadPart = std::max<int64_t>(time_us, 0) * 10;
        const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, at);
        PropVariantClear(&at);
        for (auto& t : tracks_) t.ended = false;
        ended_ = false;
        has_sample_ = false;
        loaded_ = false;
        return SUCCEEDED(hr) ? 0 : kErrorUnknown;
    }

    // The current sample of the selected tracks, reading it on first use.
    bool current() {
        if (!loaded_) load();
        return has_sample_;
    }
    bool advance() {
        if (!current()) return false;
        has_sample_ = false;
        loaded_ = false;
        return current();
    }
    int64_t time_us() const { return sample_time_us_; }
    int32_t sample_track() const { return sample_track_; }
    int32_t sample_flags() const { return sample_flags_; }
    const std::vector<uint8_t>& sample() const { return sample_; }

private:
    const wchar_t* sniff() {
        const auto& b = source_;
        auto at = [&](size_t i, const char* text) { return b.size() >= i + std::strlen(text) && !std::memcmp(&b[i], text, std::strlen(text)); };
        if (at(4, "ftyp")) { format_ = "video/mp4"; return L"video/mp4"; }
        if (b.size() >= 4 && b[0] == 0x1a && b[1] == 0x45 && b[2] == 0xdf && b[3] == 0xa3) { format_ = "video/webm"; return L"video/webm"; }
        if (at(0, "RIFF") && at(8, "WAVE")) { format_ = "audio/wav"; return L"audio/wav"; }
        if (at(0, "ID3") || (b.size() >= 2 && b[0] == 0xff && (b[1] & 0xe0) == 0xe0)) {
            const bool adts = b.size() >= 2 && b[0] == 0xff && (b[1] & 0xf6) == 0xf0;
            format_ = adts ? "audio/aac-adts" : "audio/mpeg";
            return adts ? L"audio/aac" : L"audio/mpeg";
        }
        if (at(0, "fLaC")) { format_ = "audio/flac"; return L"audio/flac"; }
        if (b.size() >= 4 && b[0] == 0x47 && b.size() > 188 && b[188] == 0x47) { format_ = "video/mp2ts"; return L"video/mp2t"; }
        return nullptr;
    }

    bool describe(IMFMediaType* type, Track& track) {
        GUID major{}, subtype{};
        type->GetGUID(MF_MT_MAJOR_TYPE, &major);
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        const Mime* mime = mime_for(subtype);
        if (!mime) return false;
        auto& info = track.info;
        copy_string(info.mime, mime->mime);
        if (major == MFMediaType_Video) {
            UINT32 w = 0, h = 0, num = 0, den = 0;
            MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
            info.width = static_cast<int32_t>(w);
            info.height = static_cast<int32_t>(h);
            info.frame_rate = den ? static_cast<int32_t>((num + den / 2) / den) : 0;
            info.bit_rate = static_cast<int32_t>(MFGetAttributeUINT32(type, MF_MT_AVG_BITRATE, 0));
            info.max_input_size = std::max<int32_t>(1 << 20, info.width * info.height * 3 / 2);
            auto header = blob(type, MF_MT_MPEG_SEQUENCE_HEADER);
            if (subtype == MFVideoFormat_H264) {
                track.annex_b = true;
                if (!header.empty() && header[0] == 1) header = avcc_to_annex_b(header);
                std::vector<uint8_t> sps, pps;
                for (auto [at, n] : nal_units(header)) {
                    auto& into = (header[at] & 0x1f) == 7 ? sps : pps;
                    into.insert(into.end(), kStartCode, kStartCode + 4);
                    into.insert(into.end(), header.begin() + at, header.begin() + at + n);
                }
                if (!sps.empty()) track.csd.push_back(sps);
                if (!pps.empty()) track.csd.push_back(pps);
            } else if (subtype == MFVideoFormat_HEVC) {
                track.annex_b = true;
                if (!header.empty() && header[0] == 1) header = hvcc_to_annex_b(header);
                if (!header.empty()) track.csd.push_back(header);
            } else if (!header.empty()) {
                track.csd.push_back(header);
            }
        } else if (major == MFMediaType_Audio) {
            info.sample_rate = static_cast<int32_t>(MFGetAttributeUINT32(type, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0));
            info.channels = static_cast<int32_t>(MFGetAttributeUINT32(type, MF_MT_AUDIO_NUM_CHANNELS, 0));
            info.bit_rate = static_cast<int32_t>(MFGetAttributeUINT32(type, MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 0) * 8);
            info.max_input_size = 64 * 1024;
            if (subtype == MFAudioFormat_AAC) {
                info.is_adts = MFGetAttributeUINT32(type, MF_MT_AAC_PAYLOAD_TYPE, 0) == 1;
                // MF_MT_USER_DATA is HEAACWAVEINFO after WAVEFORMATEX: 12 bytes, then the AudioSpecificConfig.
                auto user = blob(type, MF_MT_USER_DATA);
                if (user.size() > 12)
                    track.csd.push_back(std::vector<uint8_t>(user.begin() + 12, user.end()));
                else if (!info.is_adts)
                    track.csd.push_back(aac_config(info.sample_rate, info.channels));
            }
        } else {
            return false;
        }
        info.csd_count = static_cast<int32_t>(track.csd.size());
        return true;
    }

    void load() {
        loaded_ = true;
        has_sample_ = false;
        if (ended_) return;
        if (std::none_of(tracks_.begin(), tracks_.end(), [](const Track& t) { return t.selected && !t.ended; })) return;
        for (int guard = 0; guard < 100000; ++guard) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0;
            Com<IMFSample> sample;
            const HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &stream, &flags, &time, sample.put());
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                LOGW("media", "AMediaExtractor: read failed 0x%08lx", static_cast<unsigned long>(hr));
                ended_ = true;
                return;
            }
            auto it = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& t) { return t.stream == stream; });
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) && it != tracks_.end()) it->ended = true;
            if (!sample || it == tracks_.end() || !it->selected) {
                if (std::none_of(tracks_.begin(), tracks_.end(), [](const Track& t) { return t.selected && !t.ended; })) {
                    ended_ = true;
                    return;
                }
                continue;
            }
            sample_ = buffer_bytes(sample.p);
            if (it->annex_b) sample_ = to_annex_b(std::move(sample_));
            sample_time_us_ = time / 10;
            sample_track_ = static_cast<int32_t>(it - tracks_.begin());
            const bool video = std::strncmp(it->info.mime, "video/", 6) == 0;
            sample_flags_ = !video || MFGetAttributeUINT32(sample.p, MFSampleExtension_CleanPoint, 0) ? 1 : 0;
            has_sample_ = true;
            return;
        }
        ended_ = true;
    }

    std::vector<uint8_t> source_;
    std::string format_ = "application/octet-stream";
    Com<IMFSourceReader> reader_;
    std::vector<Track> tracks_;
    bool loaded_ = false, has_sample_ = false, ended_ = false;
    std::vector<uint8_t> sample_;
    int64_t sample_time_us_ = 0;
    int32_t sample_track_ = -1, sample_flags_ = 0;
};

// --- Codec ----------------------------------------------------------------------------------

class Codec {
public:
    std::mutex calls;  // held by the dispatcher for each call
    explicit Codec(const Mime* mime) : mime_(mime) {}
    ~Codec() { shutdown(); }

    const char* mime() const { return mime_->mime; }

    int64_t configure(const CodecConfig& config, int64_t surface) {
        shutdown();
        config_ = config;
        surface_ = surface;
        csd_.clear();
        for (int i = 0; i < 2; ++i)
            if (config.csd[i] && config.csd_size[i] > 0)
                csd_.push_back(std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(config.csd[i]),
                                                    reinterpret_cast<const uint8_t*>(config.csd[i]) + config.csd_size[i]));
        if (!create()) return kErrorUnsupported;
        return 0;
    }

    int64_t start() {
        if (!transform_) return kErrorInvalidOperation;
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        prefix_pending_ = true;
        return 0;
    }
    void stop() {
        if (transform_) {
            transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        }
        reset_state();
    }
    void flush() {
        if (transform_) transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        reset_state();
        prefix_pending_ = true;
    }
    void set_surface(int64_t surface) { surface_ = surface; }

    int64_t input(const uint8_t* data, size_t size, int64_t time_us, int32_t flags) {
        if (!transform_) return kErrorInvalidOperation;
        if (flags & kFlagCodecConfig) {
            // Codec-specific data sent in band: keep it for the next frame.
            if (video()) {
                extra_.insert(extra_.end(), data, data + size);
            } else if (std::string(mime_->mime) == "audio/mp4a-latm") {
                csd_.assign(1, std::vector<uint8_t>(data, data + size));
                if (!create()) return kErrorUnsupported;
                start();
            }
            return 0;
        }
        std::vector<uint8_t> bytes;
        if (video()) {
            if (prefix_pending_) {
                for (auto& c : csd_) bytes.insert(bytes.end(), c.begin(), c.end());
                prefix_pending_ = false;
            }
            bytes.insert(bytes.end(), extra_.begin(), extra_.end());
            extra_.clear();
            std::vector<uint8_t> frame(data, data + size);
            if (annex_b()) frame = to_annex_b(std::move(frame));
            bytes.insert(bytes.end(), frame.begin(), frame.end());
        } else {
            bytes.assign(data, data + size);
        }
        if (!bytes.empty()) {
            Com<IMFSample> sample;
            Com<IMFMediaBuffer> buffer;
            if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()), buffer.put())) || FAILED(MFCreateSample(sample.put())))
                return kErrorUnknown;
            BYTE* target = nullptr;
            buffer->Lock(&target, nullptr, nullptr);
            std::memcpy(target, bytes.data(), bytes.size());
            buffer->Unlock();
            buffer->SetCurrentLength(static_cast<DWORD>(bytes.size()));
            sample->AddBuffer(buffer.p);
            sample->SetSampleTime(time_us * 10);
            last_input_us_ = time_us;
            for (int attempt = 0;; ++attempt) {
                const HRESULT hr = transform_->ProcessInput(0, sample.p, 0);
                if (hr == MF_E_NOTACCEPTING) {
                    // Decoded output must be collected first; never drop input.
                    if (!pull() && attempt >= 8) {
                        LOGW("media", "AMediaCodec %s: decoder accepts no input", mime_->mime);
                        return kErrorUnknown;
                    }
                    continue;
                }
                if (FAILED(hr)) {
                    LOGW("media", "AMediaCodec %s: input rejected 0x%08lx", mime_->mime, static_cast<unsigned long>(hr));
                    return kErrorMalformed;
                }
                break;
            }
        }
        if (flags & kFlagEndOfStream) {
            transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            draining_ = true;
        }
        return 0;
    }

    // 1: output format changed; 0: a buffer is described in info (and copied
    // to dst when it fits); -1: nothing yet; -3: dst is smaller than info->size.
    int64_t output(OutputInfo& info, uint8_t* dst, size_t capacity) {
        if (!transform_) return kErrorInvalidOperation;
        if (frames_.empty()) pull();
        if (format_pending_ && (!frames_.empty() || format_ready_)) {
            format_pending_ = false;
            return 1;
        }
        if (frames_.empty()) {
            if (draining_ && !eos_sent_) {
                eos_sent_ = true;
                info = {last_output_us_, ++sequence_, 0, kFlagEndOfStream};
                return 0;
            }
            return -1;
        }
        Frame& frame = frames_.front();
        info = {frame.time_us, frame.sequence, static_cast<int32_t>(frame.bytes.size()), 0};
        if (draining_ && frames_.size() == 1 && !more_output()) {
            info.flags |= kFlagEndOfStream;
            eos_sent_ = true;
        }
        if (!surface_ && !frame.bytes.empty()) {
            if (!dst || capacity < frame.bytes.size()) return -3;
            std::memcpy(dst, frame.bytes.data(), frame.bytes.size());
        }
        if (surface_ && video()) held_[frame.sequence] = std::move(frame);
        frames_.pop_front();
        return 0;
    }

    void release(uint64_t sequence, bool render) {
        auto it = held_.find(sequence);
        if (it == held_.end()) return;
        if (render && surface_) publish(it->second);
        held_.erase(it);
        // A Surface keeps only the newest frame: drop older unreleased ones.
        while (held_.size() > 8) held_.erase(held_.begin());
    }

    void format(OutputFormat& out) const {
        std::memset(&out, 0, sizeof out);
        if (video()) {
            copy_string(out.mime, "video/raw");
            out.width = static_cast<int32_t>(width_);
            out.height = static_cast<int32_t>(height_);
            out.stride = static_cast<int32_t>(width_);
            out.slice_height = static_cast<int32_t>(height_);
            out.color_format = kColorYuv420SemiPlanar;
            out.crop_left = crop_.left;
            out.crop_top = crop_.top;
            out.crop_right = crop_.right - 1;
            out.crop_bottom = crop_.bottom - 1;
        } else {
            copy_string(out.mime, "audio/raw");
            out.sample_rate = static_cast<int32_t>(rate_);
            out.channels = static_cast<int32_t>(channels_);
            out.pcm_encoding = 2;  // ENCODING_PCM_16BIT
        }
    }

private:
    struct Frame {
        int64_t time_us = 0;
        uint64_t sequence = 0;
        std::vector<uint8_t> bytes;
    };

    bool video() const { return mime_->video; }
    bool annex_b() const { return *mime_->subtype == MFVideoFormat_H264 || *mime_->subtype == MFVideoFormat_HEVC; }

    void reset_state() {
        frames_.clear();
        held_.clear();
        extra_.clear();
        draining_ = eos_sent_ = false;
    }
    void shutdown() {
        reset_state();
        transform_.reset();
    }

    bool create() {
        if (!media_foundation()) return false;
        transform_.reset();
        MFT_REGISTER_TYPE_INFO input{video() ? MFMediaType_Video : MFMediaType_Audio, *mime_->subtype};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        const UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
        HRESULT hr = MFTEnumEx(video() ? MFT_CATEGORY_VIDEO_DECODER : MFT_CATEGORY_AUDIO_DECODER, flags, &input, nullptr, &activates, &count);
        for (UINT32 i = 0; i < count; ++i) {
            if (!transform_ && SUCCEEDED(hr)) {
                IMFTransform* t = nullptr;
                if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&t)))) {
                    transform_.p = t;
                    if (!set_types()) transform_.reset();
                }
            }
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
        if (!transform_) {
            LOGW("media", "AMediaCodec: no Windows decoder for %s", mime_->mime);
            return false;
        }
        LOGI("media", "AMediaCodec: %s decoder on Media Foundation", mime_->mime);
        return true;
    }

    bool set_types() {
        Com<IMFAttributes> attributes;
        if (SUCCEEDED(transform_->GetAttributes(attributes.put())) && attributes) attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        Com<IMFMediaType> in;
        if (FAILED(MFCreateMediaType(in.put()))) return false;
        if (video()) {
            in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            in->SetGUID(MF_MT_SUBTYPE, *mime_->subtype);
            in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            if (config_.width > 0 && config_.height > 0) MFSetAttributeSize(in.p, MF_MT_FRAME_SIZE, config_.width, config_.height);
        } else {
            in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            in->SetGUID(MF_MT_SUBTYPE, *mime_->subtype);
            if (config_.sample_rate > 0) in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, config_.sample_rate);
            if (config_.channels > 0) in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, config_.channels);
            if (*mime_->subtype == MFAudioFormat_AAC) {
                const bool adts = config_.is_adts != 0;
                std::vector<uint8_t> user(12, 0);
                user[0] = adts ? 1 : 0;  // wPayloadType
                user[2] = 0xfe;          // wAudioProfileLevelIndication: unknown
                if (!adts) {
                    auto asc = !csd_.empty() ? csd_[0] : aac_config(config_.sample_rate, config_.channels);
                    user.insert(user.end(), asc.begin(), asc.end());
                }
                in->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, adts ? 1 : 0);
                in->SetBlob(MF_MT_USER_DATA, user.data(), static_cast<UINT32>(user.size()));
            }
        }
        HRESULT hr = transform_->SetInputType(0, in.p, 0);
        if (FAILED(hr)) {
            LOGW("media", "AMediaCodec %s: input type rejected 0x%08lx", mime_->mime, static_cast<unsigned long>(hr));
            return false;
        }
        return set_output_type();
    }

    bool set_output_type() {
        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> type;
            const HRESULT hr = transform_->GetOutputAvailableType(0, i, type.put());
            if (FAILED(hr)) break;
            GUID subtype{};
            type->GetGUID(MF_MT_SUBTYPE, &subtype);
            const bool wanted = video() ? subtype == MFVideoFormat_NV12
                                        : subtype == MFAudioFormat_PCM && MFGetAttributeUINT32(type.p, MF_MT_AUDIO_BITS_PER_SAMPLE, 16) == 16;
            if (!wanted) continue;
            if (FAILED(transform_->SetOutputType(0, type.p, 0))) continue;
            Com<IMFMediaType> actual;
            if (FAILED(transform_->GetOutputCurrentType(0, actual.put()))) return false;
            if (video()) {
                UINT32 w = 0, h = 0;
                MFGetAttributeSize(actual.p, MF_MT_FRAME_SIZE, &w, &h);
                width_ = w;
                height_ = h;
                MFVideoArea area{};
                if (SUCCEEDED(actual->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof area, nullptr)) &&
                    area.Area.cx > 0 && area.Area.cy > 0)
                    crop_ = {area.OffsetX.value, area.OffsetY.value, area.OffsetX.value + area.Area.cx, area.OffsetY.value + area.Area.cy};
                else if (config_.width > 0 && config_.height > 0 && static_cast<UINT32>(config_.width) <= w && static_cast<UINT32>(config_.height) <= h)
                    crop_ = {0, 0, config_.width, config_.height};
                else
                    crop_ = {0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
            } else {
                rate_ = MFGetAttributeUINT32(actual.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
                channels_ = MFGetAttributeUINT32(actual.p, MF_MT_AUDIO_NUM_CHANNELS, 0);
            }
            format_pending_ = true;
            format_ready_ = !video() || (width_ && height_);
            return true;
        }
        // Video decoders offer output types only once they have parsed a frame.
        if (video()) {
            format_ready_ = false;
            return true;
        }
        return false;
    }

    bool more_output() {
        pull();
        return !frames_.empty();
    }

    // Collects every frame the decoder can produce now. False if none.
    bool pull() {
        bool any = false;
        for (int guard = 0; guard < 256; ++guard) {
            MFT_OUTPUT_STREAM_INFO stream{};
            if (FAILED(transform_->GetOutputStreamInfo(0, &stream))) return any;
            Com<IMFSample> own;
            if (!(stream.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
                Com<IMFMediaBuffer> buffer;
                const DWORD size = std::max<DWORD>(stream.cbSize, video() ? width_ * height_ * 2 + 4096 : 64 * 1024);
                if (FAILED(MFCreateMemoryBuffer(size, buffer.put())) || FAILED(MFCreateSample(own.put()))) return any;
                own->AddBuffer(buffer.p);
            }
            MFT_OUTPUT_DATA_BUFFER out{};
            out.pSample = own.p;
            DWORD status = 0;
            const HRESULT hr = transform_->ProcessOutput(0, 1, &out, &status);
            if (out.pEvents) out.pEvents->Release();
            Com<IMFSample> produced;
            if (out.pSample && out.pSample != own.p) produced.p = out.pSample;
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return any;
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (!set_output_type()) return any;
                format_pending_ = true;
                continue;
            }
            if (FAILED(hr)) {
                LOGW("media", "AMediaCodec %s: decode failed 0x%08lx", mime_->mime, static_cast<unsigned long>(hr));
                return any;
            }
            IMFSample* sample = produced ? produced.p : own.p;
            Frame frame;
            LONGLONG time = 0;
            frame.time_us = SUCCEEDED(sample->GetSampleTime(&time)) ? time / 10 : last_input_us_;
            frame.sequence = ++sequence_;
            frame.bytes = video() ? planar(sample) : buffer_bytes(sample);
            if (!video() && frame.bytes.empty()) continue;
            last_output_us_ = frame.time_us;
            frames_.push_back(std::move(frame));
            any = true;
        }
        return any;
    }

    // Tightly packed NV12 (stride = width, slice height = height).
    std::vector<uint8_t> planar(IMFSample* sample) {
        std::vector<uint8_t> out;
        if (!width_ || !height_) return out;
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->GetBufferByIndex(0, buffer.put()))) return out;
        out.resize(static_cast<size_t>(width_) * height_ * 3 / 2);
        Com<IMF2DBuffer> two;
        BYTE* scan = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(two.put()))) && SUCCEEDED(two->Lock2D(&scan, &pitch))) {
            for (UINT32 y = 0; y < height_; ++y) std::memcpy(&out[size_t(y) * width_], scan + ptrdiff_t(pitch) * y, width_);
            const BYTE* chroma = scan + ptrdiff_t(pitch) * height_;
            for (UINT32 y = 0; y < height_ / 2; ++y)
                std::memcpy(&out[size_t(width_) * height_ + size_t(y) * width_], chroma + ptrdiff_t(pitch) * y, width_);
            two->Unlock2D();
            return out;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) return {};
        std::memcpy(out.data(), data, std::min<size_t>(length, out.size()));
        buffer->Unlock();
        return out;
    }

    // NV12 to the Surface's RGBA frame, cropped, with BT.601/709 limited range.
    void publish(const Frame& frame) {
        const LONG w = crop_.right - crop_.left, h = crop_.bottom - crop_.top;
        if (w <= 0 || h <= 0 || frame.bytes.size() < size_t(width_) * height_ * 3 / 2) return;
        const bool hd = h >= 720;
        const int rv = hd ? 459 : 409, gu = hd ? 55 : 100, gv = hd ? 136 : 208, bu = hd ? 541 : 516;
        rgba_.resize(size_t(w) * h * 4);
        const uint8_t* luma = frame.bytes.data();
        const uint8_t* chroma = luma + size_t(width_) * height_;
        for (LONG y = 0; y < h; ++y) {
            const uint8_t* yl = luma + size_t(y + crop_.top) * width_ + crop_.left;
            const uint8_t* cl = chroma + size_t((y + crop_.top) / 2) * width_ + (crop_.left & ~1);
            uint8_t* out = &rgba_[size_t(y) * w * 4];
            for (LONG x = 0; x < w; ++x) {
                const int c = (yl[x] - 16) * 298, u = cl[(x & ~1)] - 128, v = cl[(x & ~1) + 1] - 128;
                out[x * 4 + 0] = static_cast<uint8_t>(std::clamp((c + rv * v + 128) >> 8, 0, 255));
                out[x * 4 + 1] = static_cast<uint8_t>(std::clamp((c - gu * u - gv * v + 128) >> 8, 0, 255));
                out[x * 4 + 2] = static_cast<uint8_t>(std::clamp((c + bu * u + 128) >> 8, 0, 255));
                out[x * 4 + 3] = 255;
            }
        }
        media_publish_frame(surface_, static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgba_.data());
    }

    const Mime* mime_;
    CodecConfig config_{};
    int64_t surface_ = 0;
    Com<IMFTransform> transform_;
    std::vector<std::vector<uint8_t>> csd_;
    std::vector<uint8_t> extra_, rgba_;
    std::deque<Frame> frames_;
    std::map<uint64_t, Frame> held_;
    bool prefix_pending_ = true, draining_ = false, eos_sent_ = false, format_pending_ = false, format_ready_ = false;
    uint64_t sequence_ = 0;
    int64_t last_input_us_ = 0, last_output_us_ = 0;
    UINT32 width_ = 0, height_ = 0, rate_ = 0, channels_ = 0;
    RECT crop_{};
};

std::mutex g_lock;
std::map<uint64_t, int64_t> g_windows;  // ANativeWindow of a host surface -> media handle
std::map<int64_t, std::shared_ptr<Extractor>> g_extractors;
std::map<int64_t, std::shared_ptr<Codec>> g_codecs;
int64_t g_next = 1;

template <typename T>
std::shared_ptr<T> find(std::map<int64_t, std::shared_ptr<T>>& map, int64_t id) {
    std::lock_guard<std::mutex> guard(g_lock);
    auto it = map.find(id);
    return it == map.end() ? nullptr : it->second;
}

int64_t read_fd(uint64_t fd, int64_t offset, int64_t length, std::vector<uint8_t>& bytes) {
    constexpr uint64_t kPread64 = 67, kFstat = 80;
    linux_abi::kstat st{};
    if (offset < 0 || sys(kFstat, fd, reinterpret_cast<uint64_t>(&st), 0, 0) != 0 || offset > st.st_size) return kErrorMalformed;
    if (length <= 0 || length > st.st_size - offset) length = st.st_size - offset;
    if (length <= 0) return kErrorMalformed;
    if (length > (int64_t{1} << 31)) {
        LOGW("media", "AMediaExtractor: %lld MB source is too large", static_cast<long long>(length >> 20));
        return kErrorUnsupported;
    }
    bytes.resize(static_cast<size_t>(length));
    int64_t done = 0;
    while (done < length) {
        const int64_t n = sys(kPread64, fd, reinterpret_cast<uint64_t>(bytes.data() + done), static_cast<uint64_t>(length - done),
                              static_cast<uint64_t>(offset + done));
        if (n <= 0) return kErrorMalformed;
        done += n;
    }
    return 0;
}

int64_t extractor_call(HostCall& call, uint64_t op) {
    const int64_t id = static_cast<int64_t>(call.x(1));
    if (op == kExtractorNew) {
        std::lock_guard<std::mutex> guard(g_lock);
        const int64_t created = g_next++;
        g_extractors[created] = std::make_shared<Extractor>();
        return created;
    }
    if (op == kExtractorDelete) {
        std::shared_ptr<Extractor> gone;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_extractors.find(id);
        if (it != g_extractors.end()) { gone = std::move(it->second); g_extractors.erase(it); }
        return 0;
    }
    auto x = find(g_extractors, id);
    if (!x) return kErrorInvalidOperation;
    std::lock_guard<std::mutex> serial(x->calls);
    switch (op) {
        case kExtractorSourceFd: {
            std::vector<uint8_t> bytes;
            const int64_t status = read_fd(call.x(2), static_cast<int64_t>(call.x(3)), static_cast<int64_t>(call.x(4)), bytes);
            return status ? status : x->open(std::move(bytes));
        }
        case kExtractorTrackCount: return static_cast<int64_t>(x->track_count());
        case kExtractorTrack: {
            const auto* t = x->track(call.x(2));
            if (!t || !guest_writable(call.x(3), sizeof(TrackInfo))) return kErrorInvalidOperation;
            std::memcpy(reinterpret_cast<void*>(call.x(3)), &t->info, sizeof(TrackInfo));
            return 0;
        }
        case kExtractorCsd: {
            const auto* t = x->track(call.x(2));
            if (!t || call.x(3) >= t->csd.size()) return -1;
            const auto& csd = t->csd[call.x(3)];
            if (call.x(5) >= csd.size() && guest_writable(call.x(4), csd.size()))
                std::memcpy(reinterpret_cast<void*>(call.x(4)), csd.data(), csd.size());
            return static_cast<int64_t>(csd.size());
        }
        case kExtractorSelect: return x->select(call.x(2), call.x(3) != 0);
        case kExtractorSeek: return x->seek(static_cast<int64_t>(call.x(2)));
        case kExtractorSample: {
            if (!x->current()) return -1;
            if (!guest_writable(call.x(2), sizeof(SampleInfo))) return kErrorInvalidOperation;
            SampleInfo info{x->time_us(), x->sample_track(), x->sample_flags(), static_cast<int32_t>(x->sample().size()), 0};
            std::memcpy(reinterpret_cast<void*>(call.x(2)), &info, sizeof info);
            return 0;
        }
        case kExtractorRead: {
            if (!x->current()) return -1;
            const auto& s = x->sample();
            if (call.x(3) < s.size()) return kErrorInvalidOperation;
            if (!s.empty() && !guest_writable(call.x(2), s.size())) return kErrorInvalidOperation;
            if (!s.empty()) std::memcpy(reinterpret_cast<void*>(call.x(2)), s.data(), s.size());
            return static_cast<int64_t>(s.size());
        }
        case kExtractorAdvance: return x->advance() ? 1 : 0;
        case kExtractorFileFormat: {
            const std::string format = x->file_format();
            if (call.x(3) <= format.size() || !guest_writable(call.x(2), format.size() + 1)) return kErrorInvalidOperation;
            std::memcpy(reinterpret_cast<void*>(call.x(2)), format.c_str(), format.size() + 1);
            return 0;
        }
        default: return kErrorUnsupported;
    }
}

int64_t codec_call(HostCall& call, uint64_t op) {
    const int64_t id = static_cast<int64_t>(call.x(1));
    if (op == kWindowRegister || op == kWindowHandle) {
        std::lock_guard<std::mutex> guard(g_lock);
        if (op == kWindowRegister) {
            g_windows[call.x(2)] = static_cast<int64_t>(call.x(3));
            return 0;
        }
        auto it = g_windows.find(call.x(2));
        return it == g_windows.end() ? 0 : it->second;
    }
    if (op == kCodecNew) {
        char name[64]{};
        for (size_t i = 0; i + 1 < sizeof name; ++i) {
            if (!guest_readable(call.x(2) + i, 1)) return 0;
            name[i] = *reinterpret_cast<const char*>(call.x(2) + i);
            if (!name[i]) break;
        }
        const Mime* mime = mime_named(name);
        if (!mime || *mime->subtype == MFAudioFormat_PCM) {
            LOGW("media", "AMediaCodec: no decoder for %s", name);
            return 0;
        }
        std::lock_guard<std::mutex> guard(g_lock);
        const int64_t created = g_next++;
        g_codecs[created] = std::make_shared<Codec>(mime);
        return created;
    }
    if (op == kCodecDelete) {
        std::shared_ptr<Codec> gone;
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_codecs.find(id);
        if (it != g_codecs.end()) { gone = std::move(it->second); g_codecs.erase(it); }
        return 0;
    }
    auto c = find(g_codecs, id);
    if (!c) return kErrorInvalidOperation;
    std::unique_lock<std::mutex> serial(c->calls);
    switch (op) {
        case kCodecConfigure: {
            if (!guest_readable(call.x(2), sizeof(CodecConfig))) return kErrorInvalidOperation;
            CodecConfig config;
            std::memcpy(&config, reinterpret_cast<const void*>(call.x(2)), sizeof config);
            for (int i = 0; i < 2; ++i)
                if (config.csd[i] && (config.csd_size[i] <= 0 || !guest_readable(config.csd[i], config.csd_size[i]))) config.csd[i] = 0;
            return c->configure(config, static_cast<int64_t>(call.x(3)));
        }
        case kCodecStart: return c->start();
        case kCodecStop: c->stop(); return 0;
        case kCodecFlush: c->flush(); return 0;
        case kCodecSurface: c->set_surface(static_cast<int64_t>(call.x(2))); return 0;
        case kCodecInput: {
            const uint64_t size = call.x(3);
            if (size && !guest_readable(call.x(2), size)) return kErrorInvalidOperation;
            return c->input(reinterpret_cast<const uint8_t*>(call.x(2)), size, static_cast<int64_t>(call.x(4)), static_cast<int32_t>(call.x(5)));
        }
        case kCodecOutput: {
            if (!guest_writable(call.x(2), sizeof(OutputInfo))) return kErrorInvalidOperation;
            const uint64_t dst = call.x(3), capacity = call.x(4);
            const int64_t timeout_us = static_cast<int64_t>(call.x(5));
            if (dst && capacity && !guest_writable(dst, capacity)) return kErrorInvalidOperation;
            OutputInfo info{};
            int64_t result = c->output(info, reinterpret_cast<uint8_t*>(dst), capacity);
            // Nothing decodable is queued: honour a short part of the timeout
            // so polling loops do not spin, as a device codec would block.
            if (result == -1 && timeout_us > 0) serial.unlock(), std::this_thread::sleep_for(std::chrono::microseconds(std::min<int64_t>(timeout_us, 2000)));
            std::memcpy(reinterpret_cast<void*>(call.x(2)), &info, sizeof info);
            return result;
        }
        case kCodecRelease: c->release(call.x(2), call.x(3) != 0); return 0;
        case kCodecFormat: {
            if (!guest_writable(call.x(2), sizeof(OutputFormat))) return kErrorInvalidOperation;
            OutputFormat format;
            c->format(format);
            std::memcpy(reinterpret_cast<void*>(call.x(2)), &format, sizeof format);
            return 0;
        }
        default: return kErrorUnsupported;
    }
}

}  // namespace

void art_media_codec_init() {
    register_named_function("art.mediandk", [](HostCall& call) {
        // Media Foundation objects are used from whichever guest thread calls.
        thread_local const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        (void)com;
        const uint64_t op = call.x(0);
        const int64_t result = op < kCodecNew ? extractor_call(call, op) : codec_call(call, op);
        call.ret(static_cast<uint64_t>(result));
    });
}

}  // namespace axrb
