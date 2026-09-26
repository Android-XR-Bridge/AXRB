/* The Oculus Platform SDK, as a Quest with nobody signed in.

   On the headset, libovrplatformloader.so finds the SDK's implementation
   inside Horizon, and Horizon hands it the signed-in Meta account, scoped to
   the calling app's own package and signature. Nothing running here can be
   that app, and faking the account or its entitlement is not on the table,
   so this answers the way the real SDK answers an app whose user is not
   signed in (measured on a Quest 3 with helper/platform_probe):

     - initialisation succeeds;
     - every request completes with an error message of the request's own
       type, carrying Horizon's own words for that state;
     - everything else (getters on messages, arrays, users) returns nothing.

   The game then gets to decide what a missing platform means for it, which
   is what it would do on a device that is not signed in. Message type numbers
   come from the game's own C# SDK (tools/gen_ovr_messages.py).

   When you have signed in to your own Meta account (tools/meta_signin.py),
   the platform is Meta's own instead: the PC Platform SDK from the Meta
   Horizon Link app, initialised in its standalone mode with a token Meta
   scoped to your account and this app, and every ovr_* call is passed to it.
   Entitlement, the logged-in user and user proofs are then Meta's answers
   for your account, from Meta's servers; nothing here decides them.
   QB_PLATFORM=standin keeps the signed-out stand-in. */

#include "qb_env.h"
#include "android.h"
#include "ovr_messages.h"

#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>

namespace {

/* Horizon's error for a request from an app with no signed-in user (code 2). */
const char* kNotSignedIn = "The user isn't signed in or their account state wasn't in a recoverable state.";
const int kNotSignedInCode = 2;

struct Message {
    uint64_t request = 0;
    uint32_t type = 0;
    bool error = false;
};

/* Handles are opaque to the guest: numbers well away from any memory. */
const uint64_t kMessageBase = 0x1E5700000000ull; /* clear of the two tag bits below */
const uint64_t kErrorBit = 0x0000800000000000ull;
const uint64_t kInitializeBit = 0x0000400000000000ull;

std::mutex g_lock;
std::deque<uint64_t> g_queue;
std::unordered_map<uint64_t, Message> g_messages;
uint64_t g_next_request = 1;
uint64_t g_next_message = 1;
uint64_t g_error_text = 0;
std::set<std::string> g_reported;

const std::unordered_map<std::string, uint32_t>& message_types() {
    static const std::unordered_map<std::string, uint32_t> table = [] {
        std::unordered_map<std::string, uint32_t> t;
        for (const OvrMessageType& entry : kOvrMessageTypes) t[entry.name] = entry.type;
        return t;
    }();
    return table;
}

uint64_t queue_message(uint32_t type, bool error) {
    std::lock_guard<std::mutex> held(g_lock);
    uint64_t request = g_next_request++;
    uint64_t handle = kMessageBase + g_next_message++;
    g_messages[handle] = Message{request, type, error};
    g_queue.push_back(handle);
    return request;
}

uint64_t message_of(uint64_t handle) { return handle & ~(kErrorBit | kInitializeBit); }

/* Meta's PC Platform SDK, driven with your own token. */
struct RealSdk {
    std::mutex lock;
    HMODULE library = nullptr;
    bool live = false;
    std::unordered_map<std::string, FARPROC> procs;
    std::set<std::string> missing;
};
RealSdk g_real;

/* The token tools/meta_signin.py stored for app_id: DPAPI-encrypted JSON,
   {"profile": ..., "apps": {"<app id>": "<token>"}}. */
std::string stored_token(const std::string& app_id) {
    char base[MAX_PATH] = "";
    if (!GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH)) return {};
    std::string path = std::string(base) + "\\QuestBridge\\meta.bin";
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return {};
    std::string sealed;
    char chunk[4096];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) sealed.append(chunk, n);
    std::fclose(file);
    DATA_BLOB in{(DWORD)sealed.size(), reinterpret_cast<BYTE*>(sealed.data())}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return {};
    std::string json(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    std::string key = "\"" + app_id + "\": \"";
    size_t at = json.find(key);
    std::string token;
    if (at != std::string::npos) {
        at += key.size();
        size_t end = json.find('"', at);
        if (end != std::string::npos) token = json.substr(at, end - at);
    }
    SecureZeroMemory(json.data(), json.size());
    return token;
}

FARPROC real_proc(const std::string& name) {
    auto found = g_real.procs.find(name);
    if (found != g_real.procs.end()) return found->second;
    FARPROC proc = GetProcAddress(g_real.library, name.c_str());
    g_real.procs[name] = proc;
    return proc;
}

/* Starts Meta's SDK for app_id and waits for its answer. False, and the
   stand-in stays, when there is no stored sign-in or Meta says no. */
bool real_start(const std::string& app_id) {
    std::lock_guard<std::mutex> held(g_real.lock);
    if (g_real.live) return true;
    const char* choice = QB_ENV("QB_PLATFORM");
    if (choice && std::string(choice) == "standin") return false;
    std::string token = stored_token(app_id);
    if (token.empty()) {
        std::printf("platform: no Meta sign-in stored for app %s (tools/meta_signin.py), so nobody is signed in\n",
                    app_id.c_str());
        return false;
    }
    SetDllDirectoryA("C:\\Program Files\\Meta Horizon\\Support\\oculus-runtime");
    g_real.library = LoadLibraryA("C:\\Program Files\\Meta Horizon\\Support\\oculus-runtime\\LibOVRPlatform64_1.dll");
    SetDllDirectoryA(nullptr);
    if (!g_real.library) {
        std::printf("platform: Meta's Platform SDK (the Meta Horizon Link app) is not installed\n");
        return false;
    }
    using Init = uint64_t (*)(const char*);
    using Pop = uint64_t (*)();
    using IsError = bool (*)(uint64_t);
    using Free = void (*)(uint64_t);
    auto init = reinterpret_cast<Init>(real_proc("ovr_PlatformInitializeStandaloneAccessToken"));
    auto pop = reinterpret_cast<Pop>(real_proc("ovr_PopMessage"));
    auto is_error = reinterpret_cast<IsError>(real_proc("ovr_Message_IsError"));
    auto release = reinterpret_cast<Free>(real_proc("ovr_FreeMessage"));
    if (!init || !pop || !is_error || !release) return false;
    /* Success posts no message (only a failure does, and then the game's own
       requests carry Meta's error as well), so it is live from here. */
    uint64_t started = init(token.c_str());
    SecureZeroMemory(token.data(), token.size());
    if (!started) {
        std::printf("platform: Meta's Platform SDK refused to start\n");
        return false;
    }
    g_real.live = true;
    std::printf("platform: Meta's own Platform SDK is answering for app %s, as your account\n", app_id.c_str());
    return true;
}

}  // namespace

bool GuestLibc::platform_call(const std::string& name, GuestCpu& cpu) {
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    static const bool trace = QB_ENV("QB_TRACE_PLATFORM") != nullptr;
    const std::string bare = name.substr(4); /* without "ovr_" */

    /* Initialisation starts Meta's SDK when you have signed in; the answers
       below follow either way. */
    bool initialising = name == "ovr_UnityInitWrapper" || name == "ovr_UnityInitWrapperInternal" ||
                        name == "ovr_PlatformInitializeAndroid" ||
                        (name.find("Asynchronous") != std::string::npos &&
                         (name.find("InitWrapper") != std::string::npos ||
                          name.find("PlatformInitialize") != std::string::npos));
    if (initialising) {
        const char* app = reinterpret_cast<const char*>(cpu.x[0]);
        if (app && *app) real_start(app);
    }

    /* Everything else goes to Meta's SDK once it is live, except messages
       this file made itself (an asynchronous initialisation's). */
    if (g_real.live && !initialising) {
        bool ours = false;
        if (name.compare(0, 12, "ovr_Message_") == 0 || name == "ovr_FreeMessage" ||
            name.compare(0, 10, "ovr_Error_") == 0 || name == "ovr_PlatformInitialize_GetResult") {
            std::lock_guard<std::mutex> held(g_lock);
            ours = g_messages.count(message_of(cpu.x[0])) != 0;
        }
        if (name == "ovr_PopMessage") {
            std::lock_guard<std::mutex> held(g_lock);
            ours = !g_queue.empty();
        }
        if (!ours) {
            /* A guest function pointer is arm64 code the host cannot call. */
            if (name.find("Callback") != std::string::npos) {
                ret(0);
                return true;
            }
            FARPROC proc = nullptr;
            {
                std::lock_guard<std::mutex> held(g_real.lock);
                proc = real_proc(name);
                if (!proc && g_real.missing.insert(name).second)
                    std::printf("platform: Meta's SDK has no %s; answered as before\n", name.c_str());
            }
            if (proc) {
                using Any = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                         uint64_t);
                uint64_t result = reinterpret_cast<Any>(proc)(cpu.x[0], cpu.x[1], cpu.x[2], cpu.x[3], cpu.x[4],
                                                              cpu.x[5], cpu.x[6], cpu.x[7]);
                /* A bool comes back in al with the rest of rax whatever it
                   was, and the guest reads all of w0: only the low byte is
                   the answer. Meta's bool getters are Is*, Has*, Can* and
                   Get{Is,Has,Can}*. */
                std::string last = name.substr(name.rfind('_') + 1);
                if (last.compare(0, 3, "Get") == 0) last = last.substr(3);
                if (last.compare(0, 2, "Is") == 0 || last.compare(0, 3, "Has") == 0 || last.compare(0, 3, "Can") == 0)
                    result &= 0xff;
                if (trace) std::printf("platform: %s -> %llx (Meta)\n", name.c_str(), (unsigned long long)result);
                ret(result);
                return true;
            }
        }
    }

    /* Initialisation: succeeds, as it does for a signed-out user. */
    if (name == "ovr_UnityInitWrapper" || name == "ovr_UnityInitWrapperInternal" ||
        name == "ovr_UnityInitWrapperStandalone" || name == "ovr_PlatformInitializeAndroid") {
        if (trace) std::printf("platform: %s -> initialised, nobody signed in\n", name.c_str());
        /* ovr_UnityInitWrapper answers a bool (true: initialised), which the
           C# side checks before throwing "failed to initialize"; the others
           answer ovrPlatformInitializeResult, where 0 is success. */
        ret(name == "ovr_UnityInitWrapper" ? 1 : 0);
        return true;
    }
    if (name.find("Asynchronous") != std::string::npos &&
        (name.find("InitWrapper") != std::string::npos || name.find("PlatformInitialize") != std::string::npos)) {
        uint64_t request = queue_message(message_types().at("Platform_InitializeAndroidAsynchronous"), false);
        if (trace) std::printf("platform: %s -> request %llu, initialised\n", name.c_str(), (unsigned long long)request);
        ret(request);
        return true;
    }

    /* The message queue. */
    if (name == "ovr_PopMessage") {
        std::lock_guard<std::mutex> held(g_lock);
        uint64_t handle = 0;
        if (!g_queue.empty()) {
            handle = g_queue.front();
            g_queue.pop_front();
        }
        if (trace && handle) std::printf("platform: ovr_PopMessage -> %llx\n", (unsigned long long)handle);
        ret(handle);
        return true;
    }
    if (name.compare(0, 12, "ovr_Message_") == 0 || name == "ovr_FreeMessage" ||
        name.compare(0, 10, "ovr_Error_") == 0 || name == "ovr_PlatformInitialize_GetResult") {
        std::lock_guard<std::mutex> held(g_lock);
        auto found = g_messages.find(message_of(cpu.x[0]));
        const Message* message = found == g_messages.end() ? nullptr : &found->second;
        if (trace)
            std::printf("platform: %s(%llx) on %s\n", name.c_str(), (unsigned long long)cpu.x[0],
                        message ? "a queued message" : "nothing we know");
        if (name == "ovr_FreeMessage") {
            if (found != g_messages.end()) g_messages.erase(found);
            ret(0);
        } else if (name == "ovr_Message_GetType") {
            ret(message ? message->type : 0);
        } else if (name == "ovr_Message_IsError") {
            ret(message && message->error ? 1 : 0);
        } else if (name == "ovr_Message_GetNativeMessage") {
            ret(message ? cpu.x[0] : 0); /* the message is its own native message */
        } else if (name == "ovr_Message_GetRequestID") {
            ret(message ? message->request : 0);
        } else if (name == "ovr_Message_GetError") {
            ret(message && message->error ? (cpu.x[0] | kErrorBit) : 0);
        } else if (name == "ovr_Message_GetPlatformInitialize") {
            ret(message && !message->error ? (cpu.x[0] | kInitializeBit) : 0);
        } else if (name == "ovr_PlatformInitialize_GetResult") {
            ret(0); /* success */
        } else if (name == "ovr_Error_GetCode") {
            ret(kNotSignedInCode);
        } else if (name == "ovr_Error_GetHttpCode") {
            ret(0);
        } else if (name == "ovr_Error_GetMessage" || name == "ovr_Error_GetDisplayableMessage") {
            if (!g_error_text) g_error_text = guest_string(kNotSignedIn);
            ret(g_error_text);
        } else {
            ret(0); /* any payload getter: an error message has none */
        }
        return true;
    }

    /* A request: completes, with the not-signed-in error, as a message of
       its own type. */
    auto type = message_types().find(bare);
    if (type != message_types().end()) {
        uint64_t request = queue_message(type->second, true);
        if (trace || !g_reported.count(name)) {
            std::printf("platform: %s -> request %llu, answered: not signed in\n", name.c_str(),
                        (unsigned long long)request);
            g_reported.insert(name);
        }
        ret(request);
        return true;
    }

    /* Anything else: nothing there. */
    if (trace || !g_reported.count(name)) {
        std::printf("platform: %s -> nothing\n", name.c_str());
        g_reported.insert(name);
    }
    ret(0);
    return true;
}
