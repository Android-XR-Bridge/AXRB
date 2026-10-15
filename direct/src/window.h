#pragma once

#include <windows.h>

#include <cstdint>

namespace axrb {

// The desktop window showing an Android surface (the flat mirror of a VR app).
// One per ANativeWindow, created on a dedicated message thread on first use and
// sized like the surface. Closing it ends the application, like the system
// back-to-home on a device.
HWND host_window_for(uint64_t native_window, int32_t width, int32_t height);

}  // namespace axrb
