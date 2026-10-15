// Host audio output for the guest's libaaudio.so (direct/guest/proxy/libaaudio.c):
// one WASAPI shared-mode stream on the default render device per AAudio
// stream. WASAPI converts format and rate (AUTOCONVERTPCM), so the guest's
// samples are copied as they are.

#include "audio.h"

#include "cpu.h"
#include "kernel.h"
#include "log.h"
#include "memory.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace axrb {

namespace {

constexpr int32_t kErrorUnavailable = -889, kErrorInvalidFormat = -883, kErrorInvalidHandle = -892,
                  kErrorDisconnected = -899;

struct Stream {
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    IAudioClock* clock = nullptr;
    HANDLE event = nullptr;
    UINT32 capacity = 0;
    int32_t frame_bytes = 0;
    int32_t rate = 0;
    int32_t burst = 0;
    bool started = false;
    std::mutex lock;
};

std::mutex g_streams_lock;
std::unordered_map<int64_t, Stream*> g_streams;
int64_t g_next_handle = 1;

void ensure_com() {
    thread_local bool initialized = false;
    if (!initialized) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized = true;
    }
}

Stream* stream_of(int64_t handle) {
    std::lock_guard<std::mutex> held(g_streams_lock);
    auto it = g_streams.find(handle);
    return it == g_streams.end() ? nullptr : it->second;
}

void destroy(Stream* s) {
    if (s->client && s->started) s->client->Stop();
    if (s->clock) s->clock->Release();
    if (s->render) s->render->Release();
    if (s->client) s->client->Release();
    if (s->event) CloseHandle(s->event);
    delete s;
}

int64_t open_stream(int32_t rate, int32_t channels, int32_t format, int32_t* out_rate, int32_t* out_burst,
                    int32_t* out_capacity) {
    ensure_com();
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))))
        return kErrorUnavailable;
    IMMDevice* device = nullptr;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr)) {
        LOGW("audio", "no default output device (0x%08lx)", hr);
        return kErrorUnavailable;
    }
    auto* s = new Stream;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&s->client));
    device->Release();
    if (FAILED(hr)) {
        destroy(s);
        return kErrorUnavailable;
    }
    WAVEFORMATEX* mix = nullptr;
    s->client->GetMixFormat(&mix);
    if (rate <= 0) rate = mix ? static_cast<int32_t>(mix->nSamplesPerSec) : 48000;
    if (mix) CoTaskMemFree(mix);
    if (channels <= 0 || channels > 8) channels = 2;

    WAVEFORMATEXTENSIBLE fmt{};
    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = static_cast<WORD>(channels);
    fmt.Format.nSamplesPerSec = static_cast<DWORD>(rate);
    fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    switch (format) {
        case 1: fmt.Format.wBitsPerSample = 16; fmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM; break;
        case 2: fmt.Format.wBitsPerSample = 32; fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT; break;
        case 3: fmt.Format.wBitsPerSample = 24; fmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM; break;
        case 4: fmt.Format.wBitsPerSample = 32; fmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM; break;
        default: destroy(s); return kErrorInvalidFormat;
    }
    fmt.Samples.wValidBitsPerSample = fmt.Format.wBitsPerSample;
    fmt.Format.nBlockAlign = static_cast<WORD>(channels * fmt.Format.wBitsPerSample / 8);
    fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
    static const DWORD masks[] = {0, SPEAKER_FRONT_CENTER, KSAUDIO_SPEAKER_STEREO, KSAUDIO_SPEAKER_STEREO | SPEAKER_FRONT_CENTER,
                                  KSAUDIO_SPEAKER_QUAD, KSAUDIO_SPEAKER_QUAD | SPEAKER_FRONT_CENTER, KSAUDIO_SPEAKER_5POINT1,
                                  KSAUDIO_SPEAKER_5POINT1 | SPEAKER_BACK_CENTER, KSAUDIO_SPEAKER_7POINT1_SURROUND};
    fmt.dwChannelMask = masks[channels];

    const REFERENCE_TIME duration = 400000;  // 40 ms
    hr = s->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                               AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                   AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                               duration, 0, &fmt.Format, nullptr);
    if (FAILED(hr)) {
        LOGW("audio", "IAudioClient::Initialize(%d Hz, %d ch, format %d) failed: 0x%08lx", rate, channels, format, hr);
        destroy(s);
        return kErrorInvalidFormat;
    }
    s->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s->client->SetEventHandle(s->event);
    s->client->GetBufferSize(&s->capacity);
    s->client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&s->render));
    s->client->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&s->clock));
    REFERENCE_TIME period = 0;
    s->client->GetDevicePeriod(&period, nullptr);
    s->rate = rate;
    s->frame_bytes = fmt.Format.nBlockAlign;
    s->burst = std::max<int32_t>(64, static_cast<int32_t>(period * rate / 10000000));
    if (!s->render) {
        destroy(s);
        return kErrorUnavailable;
    }
    *out_rate = rate;
    *out_burst = s->burst;
    *out_capacity = static_cast<int32_t>(s->capacity);
    std::lock_guard<std::mutex> held(g_streams_lock);
    int64_t handle = g_next_handle++;
    g_streams[handle] = s;
    LOGI("audio", "output stream %lld: %d Hz, %d channels, format %d, burst %d, capacity %u", (long long)handle, rate,
         channels, format, s->burst, s->capacity);
    return handle;
}

int32_t room(Stream* s) {
    UINT32 padding = 0;
    if (FAILED(s->client->GetCurrentPadding(&padding))) return kErrorDisconnected;
    return static_cast<int32_t>(s->capacity - padding);
}

}  // namespace

bool audio_active() {
    std::lock_guard guard(g_streams_lock);
    for (const auto& [id, stream] : g_streams) {
        std::lock_guard streamGuard(stream->lock);
        if (stream->started) return true;
    }
    return false;
}

void audio_init() {
    register_named_function("audio.open", [](HostCall& c) {
        int32_t rate = 0, burst = 0, capacity = 0;
        int64_t h = open_stream(static_cast<int32_t>(c.x(0)), static_cast<int32_t>(c.x(1)),
                                static_cast<int32_t>(c.x(2)), &rate, &burst, &capacity);
        if (h > 0) {
            guest_write(c.x(3), &rate, 4);
            guest_write(c.x(4), &burst, 4);
            guest_write(c.x(5), &capacity, 4);
        }
        c.ret(static_cast<uint64_t>(h));
    });
    register_named_function("audio.start", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        std::lock_guard<std::mutex> held(s->lock);
        if (!s->started) {
            // Start with a primed buffer of silence so the first callbacks are not an underrun.
            s->started = SUCCEEDED(s->client->Start());
        }
        c.ret(s->started ? 0 : static_cast<uint64_t>(static_cast<int64_t>(kErrorDisconnected)));
    });
    register_named_function("audio.stop", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        std::lock_guard<std::mutex> held(s->lock);
        if (s->started) s->client->Stop();
        s->started = false;
        SetEvent(s->event);
        c.ret(0);
    });
    register_named_function("audio.flush", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        std::lock_guard<std::mutex> held(s->lock);
        if (!s->started) s->client->Reset();
        c.ret(0);
    });
    register_named_function("audio.wait", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        const int64_t timeout_ns = static_cast<int64_t>(c.x(1));
        int32_t r = room(s);
        if (r < 0 || r >= s->burst || !s->started || timeout_ns <= 0) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(r)));
        DWORD ms = static_cast<DWORD>(std::min<int64_t>((timeout_ns + 999999) / 1000000, 1000));
        WaitForSingleObject(s->event, ms);
        c.ret(static_cast<uint64_t>(static_cast<int64_t>(room(s))));
    });
    register_named_function("audio.write", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        int32_t frames = std::min<int32_t>(static_cast<int32_t>(c.x(2)), room(s));
        if (frames <= 0) return c.ret(0);
        BYTE* buffer = nullptr;
        if (FAILED(s->render->GetBuffer(static_cast<UINT32>(frames), &buffer)))
            return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorDisconnected)));
        guest_read(c.x(1), buffer, static_cast<size_t>(frames) * s->frame_bytes);
        s->render->ReleaseBuffer(static_cast<UINT32>(frames), 0);
        c.ret(static_cast<uint64_t>(frames));
    });
    register_named_function("audio.position", [](HostCall& c) {
        Stream* s = stream_of(static_cast<int64_t>(c.x(0)));
        if (!s || !s->clock) return c.ret(static_cast<uint64_t>(static_cast<int64_t>(kErrorInvalidHandle)));
        ensure_com();
        UINT64 freq = 0, pos = 0, qpc = 0;
        s->clock->GetFrequency(&freq);
        s->clock->GetPosition(&pos, &qpc);
        int64_t frames = freq ? static_cast<int64_t>(pos * static_cast<UINT64>(s->rate) / freq) : 0;
        int64_t now = monotonic_ns();
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        const int64_t qpc_now_100ns = counter.QuadPart * 10000000 / frequency.QuadPart;
        int64_t time_ns = now - (qpc_now_100ns - static_cast<int64_t>(qpc)) * 100;
        guest_write(c.x(1), &frames, 8);
        guest_write(c.x(2), &time_ns, 8);
        c.ret(0);
    });
    register_named_function("audio.close", [](HostCall& c) {
        Stream* s = nullptr;
        {
            std::lock_guard<std::mutex> held(g_streams_lock);
            auto it = g_streams.find(static_cast<int64_t>(c.x(0)));
            if (it != g_streams.end()) {
                s = it->second;
                g_streams.erase(it);
            }
        }
        if (s) {
            ensure_com();
            destroy(s);
        }
    });
}

}  // namespace axrb
