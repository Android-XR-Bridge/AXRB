package android.opengl;

/**
 * EGL without a display, like the native libEGL.so proxy: there is no OpenGL
 * ES implementation, so eglGetDisplay finds none and everything else fails.
 */
public class EGL14 {
    public static final int EGL_SUCCESS = 0x3000;
    public static final int EGL_NOT_INITIALIZED = 0x3001;
    public static final int EGL_BAD_DISPLAY = 0x3008;
    public static final int EGL_DEFAULT_DISPLAY = 0;

    public static final EGLContext EGL_NO_CONTEXT = new EGLContext(0);
    public static final EGLDisplay EGL_NO_DISPLAY = new EGLDisplay(0);
    public static final EGLSurface EGL_NO_SURFACE = new EGLSurface(0);

    private static final ThreadLocal<Integer> sError = new ThreadLocal<Integer>();

    private static boolean fail(int error) {
        sError.set(error);
        return false;
    }

    public static int eglGetError() {
        Integer e = sError.get();
        sError.set(EGL_SUCCESS);
        return e != null ? e : EGL_SUCCESS;
    }

    public static EGLDisplay eglGetDisplay(int displayId) {
        return EGL_NO_DISPLAY;
    }

    public static boolean eglInitialize(EGLDisplay dpy, int[] major, int majorOffset, int[] minor, int minorOffset) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static boolean eglTerminate(EGLDisplay dpy) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static String eglQueryString(EGLDisplay dpy, int name) {
        fail(EGL_BAD_DISPLAY);
        return null;
    }

    public static boolean eglChooseConfig(EGLDisplay dpy, int[] attribList, int attribListOffset, EGLConfig[] configs,
            int configsOffset, int configSize, int[] numConfig, int numConfigOffset) {
        if (numConfig != null && numConfig.length > numConfigOffset) numConfig[numConfigOffset] = 0;
        return fail(EGL_NOT_INITIALIZED);
    }

    public static EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext shareContext,
            int[] attribList, int offset) {
        fail(EGL_NOT_INITIALIZED);
        return EGL_NO_CONTEXT;
    }

    public static EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, int[] attribList, int offset) {
        fail(EGL_NOT_INITIALIZED);
        return EGL_NO_SURFACE;
    }

    public static EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, Object win, int[] attribList,
            int offset) {
        fail(EGL_NOT_INITIALIZED);
        return EGL_NO_SURFACE;
    }

    public static boolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static boolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static boolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static boolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static boolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, int attribute, int[] value, int offset) {
        return fail(EGL_BAD_DISPLAY);
    }

    public static EGLContext eglGetCurrentContext() {
        return EGL_NO_CONTEXT;
    }

    public static EGLDisplay eglGetCurrentDisplay() {
        return EGL_NO_DISPLAY;
    }

    public static EGLSurface eglGetCurrentSurface(int readdraw) {
        return EGL_NO_SURFACE;
    }
}
