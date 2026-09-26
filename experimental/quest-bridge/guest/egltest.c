/* The setup an OpenXR application does before it draws anything. */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

void qb_guest_main(void) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK("there is a display", display != EGL_NO_DISPLAY, 1);

    EGLint major = 0, minor = 0;
    CHECK("initialising", eglInitialize(display, &major, &minor), EGL_TRUE);
    CHECK("it reports a version", major >= 1, 1);

    const EGLint wanted[] = {EGL_RENDERABLE_TYPE, 0x0040 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                             EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config = 0;
    EGLint found = 0;
    CHECK("choosing a config", eglChooseConfig(display, wanted, &config, 1, &found), EGL_TRUE);
    CHECK("one config came back", found, 1);
    CHECK("the config is not null", config != 0, 1);

    EGLint red = 0;
    eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red);
    CHECK("the config has eight bits of red", red, 8);

    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    CHECK("a context", context != EGL_NO_CONTEXT, 1);

    const EGLint surface_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attributes);
    CHECK("a surface", surface != EGL_NO_SURFACE, 1);

    CHECK("making it current", eglMakeCurrent(display, surface, surface, context), EGL_TRUE);
    CHECK("the current context is the one made", eglGetCurrentContext() == context, 1);
    CHECK("the current display agrees", eglGetCurrentDisplay() == display, 1);
    CHECK("no error was raised", eglGetError(), EGL_SUCCESS);

    const char* version = eglQueryString(display, EGL_VERSION);
    CHECK("a version string that can be read", version != 0 && strlen(version) > 0, 1);

    /* Looking a function up by name has to give something callable. */
    void* address = (void*)eglGetProcAddress("glDrawArrays");
    CHECK("a function found by name", address != 0, 1);

    CHECK("tearing down", eglDestroySurface(display, surface), EGL_TRUE);
    CHECK("and the context", eglDestroyContext(display, context), EGL_TRUE);
    CHECK("and the display", eglTerminate(display), EGL_TRUE);
}
