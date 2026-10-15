// android.media.MediaPlayer on Media Foundation. Video is decoded on the host
// into RGBA frames held per android.view.Surface handle; whoever consumes the
// surface (the OpenXR runtime's Android-surface swapchains) pulls the newest
// frame with Surface.nativeReadFrame. Audio tracks are not played.

#include "media.h"
#include "cpu.h"
#include "audio.h"

#include "kernel.h"
#include "linux.h"
#include "log.h"
#include "memory.h"

#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace axrb {
namespace vm = arun::vm;

namespace {

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

// Newest decoded frame of one Surface, top-down RGBA.
struct FrameSlot {
    std::mutex lock;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint64_t serial = 0;
};

std::mutex g_slots_lock;
std::map<int64_t, std::shared_ptr<FrameSlot>> g_slots;

std::shared_ptr<FrameSlot> slot_for(int64_t surface) {
    std::lock_guard<std::mutex> guard(g_slots_lock);
    auto& slot = g_slots[surface];
    if (!slot) slot = std::make_shared<FrameSlot>();
    return slot;
}

std::shared_ptr<FrameSlot> find_slot(int64_t surface) {
    std::lock_guard<std::mutex> guard(g_slots_lock);
    auto it = g_slots.find(surface);
    return it == g_slots.end() ? nullptr : it->second;
}

bool media_foundation() {
    static const bool ready = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    return ready;
}

class Player {
public:
    ~Player() { stop_thread(); }

    void set_source(std::vector<uint8_t> bytes) {
        stop_thread();
        source_ = std::move(bytes);
        prepared_ = false;
    }
    void set_surface(int64_t surface) {
        std::lock_guard<std::mutex> guard(lock_);
        slot_ = surface ? slot_for(surface) : nullptr;
    }
    void set_looping(bool looping) { looping_ = looping; }
    bool looping() const { return looping_; }

    bool prepare() {
        stop_thread();
        reader_.reset();
        if (source_.empty() || !media_foundation()) return false;
        Com<IStream> stream;
        stream.p = SHCreateMemStream(source_.data(), static_cast<UINT>(source_.size()));
        if (!stream) return false;
        Com<IMFByteStream> bytes;
        if (FAILED(MFCreateMFByteStreamOnStream(stream.p, bytes.put()))) return false;
        Com<IMFAttributes> attributes;
        if (FAILED(MFCreateAttributes(attributes.put(), 2))) return false;
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        HRESULT hr = MFCreateSourceReaderFromByteStream(bytes.p, attributes.p, reader_.put());
        if (FAILED(hr)) {
            LOGW("media", "MediaPlayer: source reader failed 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        Com<IMFMediaType> type;
        if (FAILED(MFCreateMediaType(type.put()))) return false;
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.p);
        if (FAILED(hr)) {
            LOGW("media", "MediaPlayer: no RGB32 output 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        Com<IMFMediaType> actual;
        if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), actual.put())))
            return false;
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(actual.p, MF_MT_FRAME_SIZE, &w, &h);
        width_ = w;
        height_ = h;
        PROPVARIANT duration;
        PropVariantInit(&duration);
        if (SUCCEEDED(reader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                        MF_PD_DURATION, &duration)) &&
            duration.vt == VT_UI8)
            duration_ms_ = static_cast<int32_t>(duration.uhVal.QuadPart / 10000);
        PropVariantClear(&duration);
        prepared_ = true;
        position_100ns_ = 0;
        LOGI("media", "MediaPlayer: prepared %ux%u, %d ms", width_, height_, duration_ms_);
        return true;
    }

    void start() {
        if (!prepared_) return;
        std::unique_lock<std::mutex> guard(lock_);
        playing_ = true;
        completed_ = false;
        if (!thread_.joinable()) {
            quit_ = false;
            thread_ = std::thread([this] { decode_loop(); });
        }
        wake_.notify_all();
    }
    void pause() {
        std::lock_guard<std::mutex> guard(lock_);
        playing_ = false;
        wake_.notify_all();
    }
    void stop() {
        stop_thread();
        playing_ = false;
    }
    void seek(int32_t ms) {
        std::lock_guard<std::mutex> guard(lock_);
        seek_100ns_ = static_cast<int64_t>(ms) * 10000;
        wake_.notify_all();
    }
    bool playing() const { return playing_ && !completed_; }
    int32_t position_ms() const { return static_cast<int32_t>(position_100ns_ / 10000); }
    int32_t duration_ms() const { return duration_ms_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    void stop_thread() {
        {
            std::lock_guard<std::mutex> guard(lock_);
            quit_ = true;
            wake_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }

    void rewind() {
        PROPVARIANT start;
        PropVariantInit(&start);
        start.vt = VT_I8;
        start.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID_NULL, start);
        PropVariantClear(&start);
    }

    void decode_loop() {
        using Clock = std::chrono::steady_clock;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Clock::time_point origin = Clock::now();
        int64_t origin_pts = -1;
        for (;;) {
            {
                std::unique_lock<std::mutex> guard(lock_);
                wake_.wait(guard, [&] { return quit_ || playing_; });
                if (quit_) break;
                if (seek_100ns_ >= 0) {
                    PROPVARIANT at;
                    PropVariantInit(&at);
                    at.vt = VT_I8;
                    at.hVal.QuadPart = seek_100ns_;
                    reader_->SetCurrentPosition(GUID_NULL, at);
                    PropVariantClear(&at);
                    seek_100ns_ = -1;
                    origin_pts = -1;
                }
            }
            DWORD flags = 0;
            LONGLONG pts = 0;
            Com<IMFSample> sample;
            HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr,
                                             &flags, &pts, sample.put());
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                LOGW("media", "MediaPlayer: decode failed 0x%08lx", static_cast<unsigned long>(hr));
                completed_ = true;
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (!looping_) {
                    completed_ = true;
                    break;
                }
                rewind();
                origin_pts = -1;
                continue;
            }
            if (!sample) continue;
            // Pace frames by presentation time from the first frame after a
            // (re)start, so pauses and loops do not make playback race.
            if (origin_pts < 0) {
                origin_pts = pts;
                origin = Clock::now();
            }
            const auto due = origin + std::chrono::microseconds((pts - origin_pts) / 10);
            {
                std::unique_lock<std::mutex> guard(lock_);
                const auto paused_at = Clock::now();
                wake_.wait_until(guard, due, [&] { return quit_ || !playing_ || seek_100ns_ >= 0; });
                if (quit_) break;
                if (!playing_) {
                    wake_.wait(guard, [&] { return quit_ || playing_; });
                    if (quit_) break;
                    origin += Clock::now() - paused_at;
                }
            }
            position_100ns_ = pts;
            publish(sample.p);
        }
        CoUninitialize();
    }

    void publish(IMFSample* sample) {
        std::shared_ptr<FrameSlot> slot;
        {
            std::lock_guard<std::mutex> guard(lock_);
            slot = slot_;
        }
        if (!slot || !width_ || !height_) return;
        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) return;
        Com<IMF2DBuffer> planar;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        BYTE* raw = nullptr;
        DWORD length = 0;
        bool locked2d = SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(planar.put()))) &&
                        SUCCEEDED(planar->Lock2D(&scan0, &pitch));
        if (!locked2d) {
            if (FAILED(buffer->Lock(&raw, nullptr, &length))) return;
            scan0 = raw;
            pitch = static_cast<LONG>(width_ * 4);
            if (length < width_ * height_ * 4) {
                buffer->Unlock();
                return;
            }
        }
        {
            std::lock_guard<std::mutex> guard(slot->lock);
            slot->width = width_;
            slot->height = height_;
            slot->rgba.resize(static_cast<size_t>(width_) * height_ * 4);
            for (uint32_t y = 0; y < height_; ++y) {
                const uint8_t* in = scan0 + static_cast<ptrdiff_t>(pitch) * y;
                uint8_t* out = slot->rgba.data() + static_cast<size_t>(y) * width_ * 4;
                for (uint32_t x = 0; x < width_; ++x) {  // BGRX -> RGBA
                    out[x * 4 + 0] = in[x * 4 + 2];
                    out[x * 4 + 1] = in[x * 4 + 1];
                    out[x * 4 + 2] = in[x * 4 + 0];
                    out[x * 4 + 3] = 255;
                }
            }
            ++slot->serial;
            // AXRB_MEDIA_DUMP=<dir>: the 30th frame of each video as a PPM.
            if (slot->serial == 30) {
                if (const char* dir = std::getenv("AXRB_MEDIA_DUMP")) {
                    std::string path = std::string(dir) + "/video-" + std::to_string(width_) + "x" +
                                       std::to_string(height_) + "-" + std::to_string(GetTickCount64()) + ".ppm";
                    if (FILE* f = std::fopen(path.c_str(), "wb")) {
                        std::fprintf(f, "P6 %u %u 255\n", width_, height_);
                        for (size_t i = 0; i < slot->rgba.size(); i += 4) std::fwrite(&slot->rgba[i], 1, 3, f);
                        std::fclose(f);
                    }
                }
            }
        }
        if (locked2d)
            planar->Unlock2D();
        else
            buffer->Unlock();
    }

    std::vector<uint8_t> source_;
    Com<IMFSourceReader> reader_;
    std::shared_ptr<FrameSlot> slot_;
    std::thread thread_;
    std::mutex lock_;
    std::condition_variable wake_;
    bool quit_ = false;
    std::atomic<bool> playing_{false}, completed_{false}, looping_{false};
    bool prepared_ = false;
    int64_t seek_100ns_ = -1;
    std::atomic<int64_t> position_100ns_{0};
    int32_t duration_ms_ = 0;
    uint32_t width_ = 0, height_ = 0;
};

std::mutex g_players_lock;
std::map<int64_t, std::shared_ptr<Player>> g_players;
int64_t g_next_player = 1;

std::shared_ptr<Player> player(vm::Frame* a) {
    std::lock_guard<std::mutex> guard(g_players_lock);
    auto it = g_players.find(vm::ArgLong(a, 0));
    return it == g_players.end() ? nullptr : it->second;
}

int64_t sys(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    GuestThread* t = current_thread();
    if (!t) return -linux_abi::EBADF_;
    return do_syscall(*t, nr, a0, a1, a2, a3, 0, 0);
}

// --- android.media.MediaPlayer ---------------------------------------------------------

void MediaPlayer_nativeCreate(vm::Thread*, vm::Frame*, vm::JValue* r) {
    std::lock_guard<std::mutex> guard(g_players_lock);
    const int64_t id = g_next_player++;
    g_players[id] = std::make_shared<Player>();
    r->j = id;
}

// (J player, I fd, J offset, J length) -> Z
void MediaPlayer_nativeSetSource(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    r->i = 0;
    auto p = player(a);
    const int32_t fd = vm::ArgInt(a, 2);
    const int64_t offset = vm::ArgLong(a, 3);
    int64_t length = vm::ArgLong(a, 5);
    if (!p || fd < 0 || offset < 0) return;
    constexpr uint64_t kPread64 = 67, kFstat = 80;
    vm::ScopedBlocking blocking(self);
    if (length < 0) {
        linux_abi::kstat st{};
        if (sys(kFstat, static_cast<uint64_t>(fd), reinterpret_cast<uint64_t>(&st), 0, 0) != 0) return;
        length = st.st_size - offset;
    }
    if (length <= 0 || length > (int64_t{1} << 31)) return;
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    int64_t done = 0;
    while (done < length) {
        int64_t n = sys(kPread64, static_cast<uint64_t>(fd), reinterpret_cast<uint64_t>(bytes.data() + done),
                        static_cast<uint64_t>(length - done), static_cast<uint64_t>(offset + done));
        if (n <= 0) return;
        done += n;
    }
    p->set_source(std::move(bytes));
    r->i = 1;
}

void MediaPlayer_nativeSetSurface(vm::Thread*, vm::Frame* a, vm::JValue*) {
    if (auto p = player(a)) p->set_surface(vm::ArgLong(a, 2));
}

// -> [I {width, height, durationMs}] or null
void MediaPlayer_nativePrepare(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    r->l = nullptr;
    auto p = player(a);
    bool ok;
    {
        vm::ScopedBlocking blocking(self);
        ok = p && p->prepare();
    }
    if (!ok) return;
    vm::Runtime* rt = self->runtime();
    vm::RtClass* int_array = rt->class_linker().FindClass(self, "[I", nullptr);
    if (!int_array) return;
    vm::Array* out = rt->heap().AllocArray(self, int_array, 3);
    if (!out) return;
    const int32_t values[3] = {static_cast<int32_t>(p->width()), static_cast<int32_t>(p->height()), p->duration_ms()};
    std::memcpy(out->data(), values, sizeof values);
    r->l = out;
}

void MediaPlayer_nativeStart(vm::Thread*, vm::Frame* a, vm::JValue*) {
    if (auto p = player(a)) p->start();
}
void MediaPlayer_nativePause(vm::Thread*, vm::Frame* a, vm::JValue*) {
    if (auto p = player(a)) p->pause();
}
void MediaPlayer_nativeStop(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    vm::ScopedBlocking blocking(self);
    if (auto p = player(a)) p->stop();
}
void MediaPlayer_nativeSeek(vm::Thread*, vm::Frame* a, vm::JValue*) {
    if (auto p = player(a)) p->seek(vm::ArgInt(a, 2));
}
void MediaPlayer_nativeSetLooping(vm::Thread*, vm::Frame* a, vm::JValue*) {
    if (auto p = player(a)) p->set_looping(vm::ArgInt(a, 2) != 0);
}
void MediaPlayer_nativeIsPlaying(vm::Thread*, vm::Frame* a, vm::JValue* r) {
    auto p = player(a);
    r->i = p && p->playing();
}
void MediaPlayer_nativePosition(vm::Thread*, vm::Frame* a, vm::JValue* r) {
    auto p = player(a);
    r->i = p ? p->position_ms() : 0;
}
void MediaPlayer_nativeRelease(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    std::shared_ptr<Player> p;
    {
        std::lock_guard<std::mutex> guard(g_players_lock);
        auto it = g_players.find(vm::ArgLong(a, 0));
        if (it == g_players.end()) return;
        p = std::move(it->second);
        g_players.erase(it);
    }
    vm::ScopedBlocking blocking(self);
    p.reset();
}

// --- android.view.Surface ----------------------------------------------------------------

// (J handle, J dst, I width, I height, J serial) -> J newest serial. Writes the
// newest frame, scaled to width x height, to dst when it is newer than serial.
void Surface_nativeReadFrame(vm::Thread*, vm::Frame* a, vm::JValue* r) {
    const int64_t handle = vm::ArgLong(a, 0);
    const uint64_t dst = static_cast<uint64_t>(vm::ArgLong(a, 2));
    const int32_t width = vm::ArgInt(a, 4), height = vm::ArgInt(a, 5);
    const int64_t seen = vm::ArgLong(a, 6);
    r->j = seen;
    auto slot = find_slot(handle);
    if (!slot || !dst || width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> guard(slot->lock);
    if (static_cast<int64_t>(slot->serial) == seen || slot->rgba.empty()) return;
    auto* out = reinterpret_cast<uint8_t*>(dst);
    if (slot->width == static_cast<uint32_t>(width) && slot->height == static_cast<uint32_t>(height)) {
        std::memcpy(out, slot->rgba.data(), slot->rgba.size());
    } else {
        for (int32_t y = 0; y < height; ++y) {
            const uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(y) * slot->height) / height);
            const uint32_t* row = reinterpret_cast<const uint32_t*>(slot->rgba.data()) + sy * slot->width;
            uint32_t* line = reinterpret_cast<uint32_t*>(out) + static_cast<size_t>(y) * width;
            for (int32_t x = 0; x < width; ++x)
                line[x] = row[(static_cast<uint64_t>(x) * slot->width) / width];
        }
    }
    r->j = static_cast<int64_t>(slot->serial);
}

void Surface_nativeRelease(vm::Thread*, vm::Frame* a, vm::JValue*) {
    std::lock_guard<std::mutex> guard(g_slots_lock);
    g_slots.erase(vm::ArgLong(a, 0));
}

}  // namespace

void media_publish_frame(int64_t surface, uint32_t width, uint32_t height, const uint8_t* rgba) {
    auto slot = slot_for(surface);
    std::lock_guard<std::mutex> guard(slot->lock);
    slot->width = width;
    slot->height = height;
    slot->rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    ++slot->serial;
}

void art_media_init() {
    register_named_function("art.media", [](HostCall& call) {
        const uint64_t op = call.x(0);
        const int64_t id = static_cast<int64_t>(call.x(1));
        if (op == 17) return call.ret(audio_active());
        if (op == 16) { std::lock_guard guard(g_slots_lock); g_slots.erase(id); return call.ret(0); }
        if (op == 0) {
            std::lock_guard guard(g_players_lock);
            const int64_t created = g_next_player++;
            g_players[created] = std::make_shared<Player>();
            return call.ret(created);
        }
        std::shared_ptr<Player> p;
        {
            std::lock_guard guard(g_players_lock);
            auto it = g_players.find(id);
            if (it != g_players.end()) {
                p = it->second;
                if (op == 12) g_players.erase(it);
            }
        }
        if (!p) return call.ret(static_cast<uint64_t>(-1));
        switch (op) {
            case 1: {
                int64_t length = static_cast<int64_t>(call.x(4));
                const int64_t offset = static_cast<int64_t>(call.x(3));
                linux_abi::kstat st{};
                if (offset < 0 || sys(80, call.x(2), reinterpret_cast<uint64_t>(&st), 0, 0) != 0 || offset > st.st_size)
                    return call.ret(-1);
                length = std::min(length, st.st_size - offset);
                if (length <= 0 || length > (int64_t{1} << 31)) return call.ret(-1);
                std::vector<uint8_t> bytes(static_cast<size_t>(length));
                int64_t done = 0;
                while (done < length) {
                    int64_t n = sys(67, call.x(2), reinterpret_cast<uint64_t>(bytes.data() + done), length - done, offset + done);
                    if (n <= 0) return call.ret(-1);
                    done += n;
                }
                p->set_source(std::move(bytes)); return call.ret(0);
            }
            case 2: p->set_surface(call.x(2)); break;
            case 3: return call.ret(p->prepare() ? 0 : -1);
            case 4: p->start(); break;
            case 5: p->pause(); break;
            case 6: p->stop(); break;
            case 7: p->seek(static_cast<int32_t>(call.x(2))); break;
            case 8: p->set_looping(call.x(2) != 0); break;
            case 9: return call.ret(p->playing());
            case 10: return call.ret(p->position_ms());
            case 11: return call.ret(p->duration_ms());
            case 12: p.reset(); break;
            case 13: return call.ret(p->width());
            case 14: return call.ret(p->height());
            case 15: return call.ret(p->looping());
            default: return call.ret(-1);
        }
        call.ret(0);
    });
    register_named_function("art.media_frame", [](HostCall& call) {
        auto slot = find_slot(call.x(0));
        uint64_t seen = call.x(4);
        if (!slot) return call.ret(0);
        std::lock_guard guard(slot->lock);
        uint32_t w = static_cast<uint32_t>(call.x(2)), h = static_cast<uint32_t>(call.x(3));
        if (slot->rgba.empty()) return call.ret(0);
        if (seen == slot->serial || !w || !h ||
            !address_space().check(call.x(1), uint64_t(w) * h * 4, linux_abi::PROT_WRITE_) ||
            !address_space().populate(call.x(1), uint64_t(w) * h * 4)) return call.ret(seen);
        auto* out = reinterpret_cast<uint32_t*>(call.x(1));
        const auto* source = reinterpret_cast<const uint32_t*>(slot->rgba.data());
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                out[size_t(y) * w + x] = source[(uint64_t(y) * slot->height / h) * slot->width + uint64_t(x) * slot->width / w];
        call.ret(slot->serial);
    });
}

void register_media_natives(vm::Runtime* rt) {
    auto reg = [rt](const char* key, vm::NativeFn fn) { rt->RegisterNative(key, fn); };
    reg("Landroid/media/MediaPlayer;.nativeCreate()J", MediaPlayer_nativeCreate);
    reg("Landroid/media/MediaPlayer;.nativeSetSource(JIJJ)Z", MediaPlayer_nativeSetSource);
    reg("Landroid/media/MediaPlayer;.nativeSetSurface(JJ)V", MediaPlayer_nativeSetSurface);
    reg("Landroid/media/MediaPlayer;.nativePrepare(J)[I", MediaPlayer_nativePrepare);
    reg("Landroid/media/MediaPlayer;.nativeStart(J)V", MediaPlayer_nativeStart);
    reg("Landroid/media/MediaPlayer;.nativePause(J)V", MediaPlayer_nativePause);
    reg("Landroid/media/MediaPlayer;.nativeStop(J)V", MediaPlayer_nativeStop);
    reg("Landroid/media/MediaPlayer;.nativeSeek(JI)V", MediaPlayer_nativeSeek);
    reg("Landroid/media/MediaPlayer;.nativeSetLooping(JZ)V", MediaPlayer_nativeSetLooping);
    reg("Landroid/media/MediaPlayer;.nativeIsPlaying(J)Z", MediaPlayer_nativeIsPlaying);
    reg("Landroid/media/MediaPlayer;.nativePosition(J)I", MediaPlayer_nativePosition);
    reg("Landroid/media/MediaPlayer;.nativeRelease(J)V", MediaPlayer_nativeRelease);
    reg("Landroid/view/Surface;.nativeReadFrame(JJIIJ)J", Surface_nativeReadFrame);
    reg("Landroid/view/Surface;.nativeRelease(J)V", Surface_nativeRelease);
}

}  // namespace axrb
