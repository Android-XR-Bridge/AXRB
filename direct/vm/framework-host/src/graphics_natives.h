// Registers the android.graphics natives (Canvas, Paint, Bitmap, BitmapFactory,
// DisplayMetrics.DENSITY_DEVICE_STABLE). Internal to arun_framework_host.
#pragma once

#include "arun/status.h"

namespace arun::vm {
class Runtime;
}

namespace arun::fw {

Status RegisterGraphicsNatives(vm::Runtime* rt, int device_density);

}  // namespace arun::fw