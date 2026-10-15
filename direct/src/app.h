#pragma once

#include <map>
#include <string>
#include <vector>

namespace axrb {

// An Android application session: what main.cpp knows from the command line.
struct AppOptions {
    std::wstring apk;           // host path of the (patched) APK, used in place
    std::wstring obb_dir;       // host directory with the game's downloaded files (optional)
    std::wstring asset_list;    // file of "guest<TAB>host" content files to mount (optional)
    std::string activity;       // launcher activity override (optional)
    std::wstring data_root;     // persistent guest storage
    int width = 1920, height = 1080, density_dpi = 480;
};

// Mounts the package into the guest file system, extracts its native
// libraries and registers the host side of the launcher. Returns false (after
// logging why) if the APK cannot be used. `guest_program` and `env` receive
// what to exec (the launcher) and extra environment variables.
bool app_prepare(const AppOptions& options, std::string& guest_program, std::vector<std::string>& env);

// Ends the session like leaving the app on a device: the activity is paused,
// stopped and destroyed on its main thread (so the game can save), then the
// process exits. Forced after a grace period if the app does not finish.
// Also triggered by the event named in AXRB_DIRECT_STOP_EVENT.
void request_session_end(const char* why);
bool session_stop_requested();

}  // namespace axrb
