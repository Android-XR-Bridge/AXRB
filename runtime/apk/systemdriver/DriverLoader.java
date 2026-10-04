package com.oculus.systemdriver;

/** Compatibility entry point for older Oculus OpenXR loaders. */
public final class DriverLoader {
    private DriverLoader() {}
    public static synchronized void loadXrRuntime() {
        System.loadLibrary("axrb_legacy_driver");
        nativeLoad();
    }
    public static long getProcAddr(String name) { return nativeGetProcAddr(name); }
    private static native void nativeLoad();
    private static native long nativeGetProcAddr(String name);
}
