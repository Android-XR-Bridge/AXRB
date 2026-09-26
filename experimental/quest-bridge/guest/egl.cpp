/* EGL, as far as a headset application needs it.

   An OpenXR application uses EGL only to get itself a context and then draws
   into swapchain images the runtime owns, so there is no window and no buffer
   to swap here. These calls keep the bookkeeping the application expects and
   hand back handles it can carry around; the drawing itself is the OpenGL ES
   driver, which is already answering on this side.

   eglGetProcAddress is the one that does real work: it gives back the address
   of a thunk, so a function the application looks up by name is callable in
   exactly the same way as one it linked against. */

#include "android.h"

#include <cstring>

namespace {

/* Handles are small distinct numbers. Nothing dereferences them but us. */
const uint64_t kDisplay = 0xe61d15;
const uint64_t kConfig = 0xe6c0f6;
const uint64_t kContext = 0xe6c047;
const uint64_t kSurface = 0xe65f0e;

const uint64_t kTrue = 1;
const uint64_t kFalse = 0;

}  // namespace

bool GuestLibc::egl_call(const std::string& name, GuestCpu& cpu) {
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](uint64_t value) { cpu.x[0] = value; };
    auto write32 = [&](uint64_t va, uint32_t value) {
        uint8_t* p = guest_ptr(mem, va, 4);
        if (p) std::memcpy(p, &value, 4);
    };

    /* The few GLES queries an engine makes to learn what GPU it is on
       (Unreal does, before choosing Vulkan): a Quest 3's Adreno 740. Nothing
       is drawn through GL here. */
    if (name.compare(0, 2, "gl") == 0) {
        static const char* const kExtensions[] = {
            "GL_OES_EGL_image", "GL_OES_EGL_image_external", "GL_OES_EGL_image_external_essl3",
            "GL_OES_texture_float", "GL_OES_texture_half_float", "GL_OES_texture_float_linear",
            "GL_EXT_color_buffer_float", "GL_EXT_color_buffer_half_float", "GL_EXT_texture_filter_anisotropic",
            "GL_KHR_texture_compression_astc_ldr", "GL_EXT_multisampled_render_to_texture", "GL_OVR_multiview",
            "GL_OVR_multiview2", "GL_EXT_disjoint_timer_query", "GL_EXT_debug_marker", "GL_KHR_debug"};
        const int kCount = (int)(sizeof(kExtensions) / sizeof(kExtensions[0]));
        if (name == "glGetString") {
            static uint64_t vendor = guest_string("Qualcomm"), renderer = guest_string("Adreno (TM) 740"),
                            version = guest_string("OpenGL ES 3.2 V@0615.65 (GIT@2b5f5bcf4f, Ie2c5a79ae0, 1693487616) (Date:08/31/23)"),
                            glsl = guest_string("OpenGL ES GLSL ES 3.20"), extensions = [&] {
                                std::string all;
                                for (const char* one : kExtensions) (all += one) += ' ';
                                return guest_string(all);
                            }();
            uint32_t which = (uint32_t)arg(0);
            ret(which == 0x1F00 ? vendor : which == 0x1F01 ? renderer : which == 0x1F02 ? version
                : which == 0x8B8C ? glsl : which == 0x1F03 ? extensions : 0);
            return true;
        }
        if (name == "glGetStringi") {
            static uint64_t each[64] = {};
            uint32_t index = (uint32_t)arg(1);
            if ((uint32_t)arg(0) != 0x1F03 || index >= (uint32_t)kCount) {
                ret(0);
                return true;
            }
            if (!each[index]) each[index] = guest_string(kExtensions[index]);
            ret(each[index]);
            return true;
        }
        if (name == "glGetIntegerv") {
            uint32_t which = (uint32_t)arg(0);
            int32_t value = which == 0x821D ? kCount                      /* GL_NUM_EXTENSIONS */
                            : which == 0x821B ? 3 : which == 0x821C ? 2     /* major, minor version */
                            : which == 0x0D33 ? 16384                       /* GL_MAX_TEXTURE_SIZE */
                            : which == 0x851C ? 16384                       /* GL_MAX_CUBE_MAP_TEXTURE_SIZE */
                            : which == 0x8869 ? 32                          /* GL_MAX_VERTEX_ATTRIBS */
                            : which == 0x8872 ? 16 : 0;                     /* GL_MAX_TEXTURE_IMAGE_UNITS */
            write32(arg(1), (uint32_t)value);
            return true;
        }
        if (name == "glGetError") {
            ret(0);
            return true;
        }
        return false;
    }
    if (name == "eglGetDisplay" || name == "eglGetPlatformDisplay") {
        ret(kDisplay);
        return true;
    }
    if (name == "eglInitialize") {
        write32(arg(1), 1); /* major */
        write32(arg(2), 5); /* minor */
        ret(kTrue);
        return true;
    }
    if (name == "eglChooseConfig" || name == "eglGetConfigs") {
        /* One configuration, which is the one the runtime is already using. */
        bool choosing = name == "eglChooseConfig";
        uint64_t out = choosing ? arg(2) : arg(1);
        uint64_t room = choosing ? arg(3) : arg(2);
        uint64_t count_out = choosing ? arg(4) : arg(3);
        if (room >= 1 && out) {
            uint64_t handle = kConfig;
            uint8_t* p = guest_ptr(mem, out, 8);
            if (p) std::memcpy(p, &handle, 8);
        }
        write32(count_out, room >= 1 ? 1 : 0);
        ret(kTrue);
        return true;
    }
    if (name == "eglGetConfigAttrib") {
        /* Answers for the attributes an application actually asks about. */
        uint32_t attribute = (uint32_t)arg(2);
        uint32_t value = 0;
        switch (attribute) {
            case 0x3024: value = 8; break;   /* EGL_RED_SIZE */
            case 0x3023: value = 8; break;   /* EGL_GREEN_SIZE */
            case 0x3022: value = 8; break;   /* EGL_BLUE_SIZE */
            case 0x3021: value = 8; break;   /* EGL_ALPHA_SIZE */
            case 0x3025: value = 24; break;  /* EGL_DEPTH_SIZE */
            case 0x3026: value = 8; break;   /* EGL_STENCIL_SIZE */
            case 0x3031: value = 0; break;   /* EGL_SAMPLES */
            case 0x3028: value = 1; break;   /* EGL_CONFIG_ID */
            case 0x3033: value = 0x0005; break; /* EGL_SURFACE_TYPE: window and pbuffer */
            case 0x3040: value = 0x0040; break; /* EGL_RENDERABLE_TYPE: ES3 */
            default: value = 0; break;
        }
        write32(arg(3), value);
        ret(kTrue);
        return true;
    }
    if (name == "eglCreateContext") {
        ret(kContext);
        return true;
    }
    if (name == "eglCreatePbufferSurface" || name == "eglCreateWindowSurface" ||
        name == "eglCreatePlatformWindowSurface") {
        ret(kSurface);
        return true;
    }
    if (name == "eglQuerySurface") {
        uint32_t attribute = (uint32_t)arg(2);
        /* The size of a surface nothing is presented through. */
        write32(arg(3), attribute == 0x3057 ? 16 : attribute == 0x3056 ? 16 : 0);
        ret(kTrue);
        return true;
    }
    if (name == "eglQueryString") {
        /* A string has to live where the guest can read it, so these are put
           on the heap once and kept. */
        static const char* answers[] = {"1.5", "Quest Bridge", "Quest Bridge EGL", ""};
        uint32_t which = (uint32_t)arg(1);
        const char* text = which == 0x3054 ? answers[0]    /* EGL_VERSION */
                           : which == 0x3053 ? answers[1]  /* EGL_VENDOR */
                           : which == 0x3055 ? answers[2]  /* EGL_CLIENT_APIS */
                                             : answers[3]; /* EGL_EXTENSIONS */
        auto found = strings.find(text);
        if (found == strings.end()) {
            GuestCpu scratch = cpu;
            scratch.x[0] = std::strlen(text) + 1;
            call("malloc", scratch);
            uint64_t where = scratch.x[0];
            char* to = reinterpret_cast<char*>(guest_ptr(mem, where, std::strlen(text) + 1));
            if (to) std::strcpy(to, text);
            strings[text] = where;
            found = strings.find(text);
        }
        ret(found->second);
        return true;
    }
    if (name == "eglGetProcAddress") {
        /* Handing back a thunk means a function found by name is called in
           exactly the same way as one that was linked against. */
        const char* wanted = reinterpret_cast<const char*>(guest_ptr(mem, arg(0), 1));
        if (!wanted || !*wanted) {
            ret(0);
            return true;
        }
        uint64_t defined = image->linker.lookup(wanted);
        ret(defined ? defined : image->linker.thunk_for(wanted));
        return true;
    }
    if (name == "eglGetCurrentContext") {
        ret(kContext);
        return true;
    }
    if (name == "eglGetCurrentDisplay") {
        ret(kDisplay);
        return true;
    }
    if (name == "eglGetCurrentSurface") {
        ret(kSurface);
        return true;
    }
    if (name == "eglGetError") {
        ret(0x3000); /* EGL_SUCCESS */
        return true;
    }
    if (name == "eglMakeCurrent" || name == "eglBindAPI" || name == "eglSwapBuffers" ||
        name == "eglSwapInterval" || name == "eglTerminate" || name == "eglDestroyContext" ||
        name == "eglDestroySurface" || name == "eglReleaseThread" || name == "eglWaitClient" ||
        name == "eglWaitGL" || name == "eglWaitNative" || name == "eglSurfaceAttrib") {
        ret(kTrue);
        return true;
    }
    (void)kFalse;
    return false;
}
