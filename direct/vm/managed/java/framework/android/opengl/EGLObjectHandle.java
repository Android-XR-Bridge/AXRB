package android.opengl;

public abstract class EGLObjectHandle {
    private final long mHandle;

    protected EGLObjectHandle(long handle) {
        mHandle = handle;
    }

    public long getNativeHandle() {
        return mHandle;
    }

    public int hashCode() {
        return (int) (mHandle ^ (mHandle >>> 32));
    }
}
