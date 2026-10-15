package android.view;

import android.graphics.SurfaceTexture;

/**
 * A producer endpoint for pixels. Surfaces here are handles (mNativeObject)
 * into the host's window table: ANativeWindow_fromSurface hands native code a
 * window for the handle, and Vulkan/EGL surfaces created on it present to the
 * host (or nowhere, for surfaces nobody shows).
 */
public class Surface implements android.os.Parcelable {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_270 = 3;

    private static long sNextHandle = 1;

    /** Host window handle; 0 once released. Read by libandroid through JNI. */
    long mNativeObject;
    private final String mName;
    int mWidth;
    int mHeight;

    public static final android.os.Parcelable.Creator<Surface> CREATOR = null;

    Surface(String name, int width, int height) {
        mName = name;
        mWidth = width;
        mHeight = height;
        mNativeObject = nativeCreate(name, width, height);
    }

    public Surface(SurfaceTexture surfaceTexture) {
        this("SurfaceTexture", surfaceTexture.mWidth, surfaceTexture.mHeight);
    }

    /** Hidden: creates a surface not attached to anything visible. */
    public Surface() {
        this("Surface", 0, 0);
    }

    private static long nativeCreate(String name, int width, int height) {
        synchronized (Surface.class) {
            return sNextHandle++;
        }
    }

    void setSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    public boolean isValid() {
        return mNativeObject != 0;
    }

    /** Frame rate hints have no effect on the host. */
    public void setFrameRate(float frameRate, int compatibility) {}

    public void setFrameRate(float frameRate, int compatibility, int changeFrameRateStrategy) {}

    public void release() {
        if (mNativeObject != 0) nativeRelease(mNativeObject);
        mNativeObject = 0;
    }

    /** Hidden: the host handle that producers (MediaPlayer) draw into. */
    public long getNativeHandle() {
        return mNativeObject;
    }

    /**
     * Hidden (AXRB): copies the newest produced frame, scaled to width x height
     * RGBA, to native memory at dst when it is newer than serial. Returns the
     * serial of the newest frame.
     */
    public long readFrame(long dst, int width, int height, long serial) {
        return mNativeObject != 0 ? nativeReadFrame(mNativeObject, dst, width, height, serial) : serial;
    }

    private static native long nativeReadFrame(long handle, long dst, int width, int height, long serial);

    private static native void nativeRelease(long handle);

    public android.graphics.Canvas lockCanvas(android.graphics.Rect inOutDirty) {
        throw new IllegalArgumentException("lockCanvas is not supported on this surface");
    }

    public void unlockCanvasAndPost(android.graphics.Canvas canvas) {}

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(android.os.Parcel dest, int flags) {}

    public String toString() {
        return "Surface(name=" + mName + ")/@0x" + Long.toHexString(mNativeObject);
    }
}
