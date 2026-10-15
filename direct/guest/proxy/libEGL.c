// libEGL.so for AXRB Direct: there is no OpenGL ES implementation, so EGL has
// no display. Applications that offer Vulkan (Unity, Unreal) use that instead;
// libraries that merely probe EGL (eglGetCurrentContext) see no context.
#include <stddef.h>
#include <stdint.h>

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLNativeWindowType;
typedef void* EGLClientBuffer;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);

#define EGL_FALSE 0
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_DISPLAY 0x3008

static __thread EGLint g_error = 0x3000;  // EGL_SUCCESS

static EGLBoolean fail(EGLint error) {
    g_error = error;
    return EGL_FALSE;
}

EGLint eglGetError(void) {
    EGLint e = g_error;
    g_error = 0x3000;
    return e;
}

EGLDisplay eglGetDisplay(void* display_id) { return NULL; }
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display, const intptr_t* attrib_list) { return NULL; }
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) { return fail(EGL_BAD_DISPLAY); }
EGLBoolean eglTerminate(EGLDisplay dpy) { return fail(EGL_BAD_DISPLAY); }
const char* eglQueryString(EGLDisplay dpy, EGLint name) {
    g_error = EGL_BAD_DISPLAY;
    return NULL;
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* procname) { return NULL; }
EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
    return fail(EGL_NOT_INITIALIZED);
}
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size,
                           EGLint* num_config) {
    if (num_config) *num_config = 0;
    return fail(EGL_NOT_INITIALIZED);
}
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value) {
    return fail(EGL_NOT_INITIALIZED);
}
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win,
                                  const EGLint* attrib_list) {
    g_error = EGL_NOT_INITIALIZED;
    return NULL;
}
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
    g_error = EGL_NOT_INITIALIZED;
    return NULL;
}
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value) {
    return fail(EGL_NOT_INITIALIZED);
}
EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    return fail(EGL_NOT_INITIALIZED);
}
EGLBoolean eglBindAPI(EGLenum api) { return 1; }
EGLenum eglQueryAPI(void) { return 0x30A0; }  // EGL_OPENGL_ES_API
EGLBoolean eglWaitClient(void) { return 1; }
EGLBoolean eglReleaseThread(void) { return 1; }
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list) {
    g_error = EGL_NOT_INITIALIZED;
    return NULL;
}
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if (!draw && !read && !ctx) return 1;
    return fail(EGL_NOT_INITIALIZED);
}
EGLContext eglGetCurrentContext(void) { return NULL; }
EGLSurface eglGetCurrentSurface(EGLint readdraw) { return NULL; }
EGLDisplay eglGetCurrentDisplay(void) { return NULL; }
EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value) {
    return fail(EGL_NOT_INITIALIZED);
}
EGLBoolean eglWaitGL(void) { return 1; }
EGLBoolean eglWaitNative(EGLint engine) { return 1; }
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, void* target) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) { return fail(EGL_NOT_INITIALIZED); }
EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) { return fail(EGL_NOT_INITIALIZED); }
EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config,
                                            const EGLint* attrib_list) {
    g_error = EGL_NOT_INITIALIZED;
    return NULL;
}
