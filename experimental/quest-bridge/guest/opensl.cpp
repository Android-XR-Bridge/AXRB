/* OpenSL ES, silent. Android's libOpenSLES.so is a system library; an engine
   creates an engine object, an output mix and an audio player whose Android
   simple buffer queue it keeps filling from the queue's callback. Here every
   object and interface is a small host block whose first word points at a
   table of thunks (qb_sl_<interface>_<method>), which is what an OpenSL
   interface is: a pointer to a pointer to a table of functions. A buffer
   enqueued is "played" for as long as it would take at the player's format,
   and then the callback runs (on the UI thread, a guest thread), so the
   engine's mixer keeps its real-time pace. Nothing is heard yet: the bytes
   are dropped. */
#include "android.h"
#include "qb_env.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

const uint32_t SL_RESULT_SUCCESS = 0, SL_RESULT_FEATURE_UNSUPPORTED = 12;

/* Interface ids: the guest imports SL_IID_* as data, each a pointer to a
   16-byte id. The address of our id is all that tells them apart. */
const char* const kIidNames[] = {"SL_IID_NULL", "SL_IID_OBJECT", "SL_IID_ENGINE", "SL_IID_ENGINECAPABILITIES",
                                 "SL_IID_PLAY", "SL_IID_VOLUME", "SL_IID_BUFFERQUEUE", "SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
                                 "SL_IID_ANDROIDCONFIGURATION", "SL_IID_EFFECTSEND", "SL_IID_PLAYBACKRATE",
                                 "SL_IID_PREFETCHSTATUS", "SL_IID_SEEK", "SL_IID_OUTPUTMIX", "SL_IID_ENVIRONMENTALREVERB",
                                 "SL_IID_PRESETREVERB", "SL_IID_EQUALIZER", "SL_IID_METADATAEXTRACTION",
                                 "SL_IID_ANDROIDEFFECT", "SL_IID_ANDROIDBUFFERQUEUESOURCE", "SL_IID_RECORD",
                                 "SL_IID_3DLOCATION", "SL_IID_MUTESOLO", "SL_IID_DYNAMICINTERFACEMANAGEMENT"};
const int kIids = (int)(sizeof(kIidNames) / sizeof(kIidNames[0]));
struct Iids {
    uint8_t id[kIids][16] = {};
    uint64_t cell[kIids] = {};
    Iids() {
        for (int i = 0; i < kIids; ++i) {
            id[i][0] = (uint8_t)(0x51 + i); /* distinct, and never read by us */
            cell[i] = reinterpret_cast<uint64_t>(&id[i][0]);
        }
    }
};
Iids& iids() {
    static Iids all;
    return all;
}
std::string iid_name(uint64_t id) {
    for (int i = 0; i < kIids; ++i)
        if (id == iids().cell[i]) return kIidNames[i];
    return "?";
}

/* An interface: the first word is the table, and the rest is ours. */
struct Interface {
    uint64_t table;
    struct Object* owner;
    std::string kind;
};
struct Object {
    Interface self;           /* the SLObjectItf */
    std::string kind;         /* engine, outputmix, player */
    std::map<std::string, Interface*> interfaces;
    /* A player's format and buffer queue. */
    uint64_t bytes_per_second = 48000 * 4;
    uint32_t state = 1;       /* SL_PLAYSTATE_STOPPED */
    uint64_t callback = 0, context = 0;
    uint32_t queued = 0;
    std::chrono::steady_clock::time_point playing_until{};
};

std::mutex g_sl_lock;

/* Methods, in the order OpenSLES.h declares them. */
const std::vector<std::string> kObjectMethods = {"Realize", "Resume", "GetState", "GetInterface", "RegisterCallback",
                                                 "AbortAsyncOperation", "Destroy", "SetPriority", "GetPriority",
                                                 "SetLossOfControlInterfaces"};
const std::vector<std::string> kEngineMethods = {
    "CreateLEDDevice", "CreateVibraDevice", "CreateAudioPlayer", "CreateAudioRecorder", "CreateMidiPlayer",
    "CreateListener", "Create3DGroup", "CreateOutputMix", "CreateMetadataExtractor", "CreateExtensionObject",
    "QueryNumSupportedInterfaces", "QuerySupportedInterfaces", "QueryNumSupportedExtensions",
    "QuerySupportedExtension", "IsExtensionSupported"};
const std::vector<std::string> kPlayMethods = {
    "SetPlayState", "GetPlayState", "GetDuration", "GetPosition", "RegisterCallback", "SetCallbackEventsMask",
    "GetCallbackEventsMask", "SetMarkerPosition", "ClearMarkerPosition", "GetMarkerPosition",
    "SetPositionUpdatePeriod", "GetPositionUpdatePeriod"};
const std::vector<std::string> kQueueMethods = {"Enqueue", "Clear", "GetState", "RegisterCallback"};
const std::vector<std::string> kVolumeMethods = {"SetVolumeLevel", "GetVolumeLevel", "GetMaxVolumeLevel", "SetMute",
                                                 "GetMute", "EnableStereoPosition", "IsEnabledStereoPosition",
                                                 "SetStereoPosition", "GetStereoPosition"};

}  // namespace

/* The table for an interface kind: thunks named qb_sl_<kind>_<method>.
   Kinds this file does not model get 24 methods that all succeed. */
static uint64_t sl_table(GuestLibc& libc, const std::string& kind) {
    static std::map<std::string, std::vector<uint64_t>> tables;
    auto found = tables.find(kind);
    if (found != tables.end()) return reinterpret_cast<uint64_t>(found->second.data());
    const std::vector<std::string>* methods = kind == "Object" ? &kObjectMethods : kind == "Engine" ? &kEngineMethods
                                            : kind == "Play" ? &kPlayMethods : kind == "Queue" ? &kQueueMethods
                                            : kind == "Volume" ? &kVolumeMethods : nullptr;
    std::vector<uint64_t>& table = tables[kind];
    if (methods)
        for (const std::string& method : *methods) table.push_back(libc.image->linker.thunk_for("qb_sl_" + kind + "_" + method));
    else
        for (int i = 0; i < 24; ++i) table.push_back(libc.image->linker.thunk_for("qb_sl_Other_" + std::to_string(i)));
    return reinterpret_cast<uint64_t>(table.data());
}

uint64_t guest_opensl_data(const std::string& name) {
    for (int i = 0; i < kIids; ++i)
        if (name == kIidNames[i]) return reinterpret_cast<uint64_t>(&iids().cell[i]);
    return 0;
}

bool GuestLibc::opensl_call(const std::string& name, GuestCpu& cpu) {
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    GuestMem& mem = image->mem;
    auto write64 = [&](uint64_t va, uint64_t value) {
        if (uint8_t* p = guest_ptr(mem, va, 8)) std::memcpy(p, &value, 8);
    };
    auto write32 = [&](uint64_t va, uint32_t value) {
        if (uint8_t* p = guest_ptr(mem, va, 4)) std::memcpy(p, &value, 4);
    };
    static const bool trace = QB_ENV("QB_TRACE_SL") != nullptr;
    auto make_object = [&](const std::string& kind) {
        auto* object = new Object;
        object->kind = kind;
        object->self.table = sl_table(*this, "Object");
        object->self.owner = object;
        object->self.kind = "Object";
        return object;
    };
    if (trace) std::printf("opensl: %s\n", name.c_str());
    if (name == "slCreateEngine") {
        Object* engine = make_object("engine");
        write64(arg(0), reinterpret_cast<uint64_t>(&engine->self));
        ret(SL_RESULT_SUCCESS);
        return true;
    }
    if (name.compare(0, 6, "qb_sl_") != 0) return false;
    std::string rest = name.substr(6);
    std::string kind = rest.substr(0, rest.find('_')), method = rest.substr(rest.find('_') + 1);
    auto* itf = reinterpret_cast<Interface*>(arg(0));
    Object* object = itf ? itf->owner : nullptr;
    std::lock_guard<std::mutex> held(g_sl_lock);

    if (kind == "Object") {
        if (method == "GetState") write32(arg(1), 2); /* SL_OBJECT_STATE_REALIZED */
        if (method == "GetInterface" && object) {
            std::string id = iid_name(arg(1));
            std::string want = id == "SL_IID_ENGINE" ? "Engine" : id == "SL_IID_PLAY" ? "Play"
                             : id == "SL_IID_ANDROIDSIMPLEBUFFERQUEUE" || id == "SL_IID_BUFFERQUEUE" ? "Queue"
                             : id == "SL_IID_VOLUME" ? "Volume" : "Other:" + id;
            if (trace) std::printf("opensl:   %s asks for %s\n", object->kind.c_str(), id.c_str());
            Interface*& made = object->interfaces[want];
            if (!made) made = new Interface{sl_table(*this, want.compare(0, 6, "Other:") == 0 ? "Other" : want), object, want};
            write64(arg(2), reinterpret_cast<uint64_t>(made));
        }
        if (method == "Destroy" && object) {
            object->state = 1;
            object->callback = 0; /* scheduled callbacks see this and stop */
        }
        if (method != "Destroy") ret(SL_RESULT_SUCCESS);
        return true;
    }
    if (kind == "Engine") {
        if (method == "CreateOutputMix") {
            write64(arg(1), reinterpret_cast<uint64_t>(&make_object("outputmix")->self));
            ret(SL_RESULT_SUCCESS);
        } else if (method == "CreateAudioPlayer") {
            Object* player = make_object("player");
            /* pAudioSrc: {pLocator, pFormat}; SLDataFormat_PCM: formatType,
               numChannels, samplesPerSec (milliHz), bitsPerSample, ... */
            const uint8_t* source = guest_ptr(mem, arg(2), 16);
            uint64_t format_va = 0;
            if (source) std::memcpy(&format_va, source + 8, 8);
            const uint8_t* format = format_va ? guest_ptr(mem, format_va, 16) : nullptr;
            if (format) {
                uint32_t channels, milli_hz, bits;
                std::memcpy(&channels, format + 4, 4);
                std::memcpy(&milli_hz, format + 8, 4);
                std::memcpy(&bits, format + 12, 4);
                uint64_t rate = milli_hz / 1000;
                if (channels && rate && bits) player->bytes_per_second = rate * channels * (bits / 8);
                if (trace)
                    std::printf("opensl:   player %u channels, %llu Hz, %u bits\n", channels, (unsigned long long)rate, bits);
            }
            write64(arg(1), reinterpret_cast<uint64_t>(&player->self));
            ret(SL_RESULT_SUCCESS);
        } else if (method == "QueryNumSupportedInterfaces" || method == "QueryNumSupportedExtensions") {
            write32(arg(method == "QueryNumSupportedInterfaces" ? 2 : 1), 0);
            ret(SL_RESULT_SUCCESS);
        } else {
            ret(SL_RESULT_FEATURE_UNSUPPORTED);
        }
        return true;
    }
    if (kind == "Play") {
        if (method == "SetPlayState" && object) object->state = (uint32_t)arg(1);
        if (method == "GetPlayState" && object) write32(arg(1), object->state);
        if (method == "GetPosition" || method == "GetDuration") write32(arg(1), 0);
        ret(SL_RESULT_SUCCESS);
        return true;
    }
    if (kind == "Queue") {
        if (method == "RegisterCallback" && object) {
            object->callback = arg(1);
            object->context = arg(2);
        } else if (method == "Clear" && object) {
            object->queued = 0;
        } else if (method == "GetState" && object) {
            write32(arg(1), object->queued);
            write32(arg(1) + 4, 0);
        } else if (method == "Enqueue" && object) {
            /* Played in real time after whatever is queued already; then the
               callback, as the device calls it when a buffer finishes. */
            uint64_t bytes = arg(2);
            auto now = std::chrono::steady_clock::now();
            if (object->playing_until < now) object->playing_until = now;
            object->playing_until += std::chrono::microseconds(bytes * 1000000 / std::max<uint64_t>(object->bytes_per_second, 1));
            ++object->queued;
            if (object->callback) {
                int delay = (int)std::chrono::duration_cast<std::chrono::milliseconds>(object->playing_until - now).count();
                uint64_t queue = arg(0);
                post_to_ui(object->callback, {queue, object->context}, std::max(delay, 0));
                /* The count drops when the callback is due; close enough for GetState. */
                if (object->queued) --object->queued;
            }
        }
        ret(SL_RESULT_SUCCESS);
        return true;
    }
    if (kind == "Volume") {
        if (method == "GetVolumeLevel" || method == "GetMaxVolumeLevel") {
            uint8_t* p = guest_ptr(mem, arg(1), 2);
            if (p) std::memset(p, 0, 2);
        }
        ret(SL_RESULT_SUCCESS);
        return true;
    }
    ret(SL_RESULT_SUCCESS); /* anything else: done */
    return true;
}
