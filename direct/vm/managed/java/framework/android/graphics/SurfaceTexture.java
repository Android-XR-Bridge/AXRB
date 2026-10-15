package android.graphics;

import android.os.Handler;

/** Image stream consumer. No producer ever delivers frames to it here. */
public class SurfaceTexture {
    public interface OnFrameAvailableListener {
        void onFrameAvailable(SurfaceTexture surfaceTexture);
    }

    public static class OutOfResourcesException extends IllegalStateException {
        public OutOfResourcesException() {}

        public OutOfResourcesException(String name) {
            super(name);
        }
    }

    private final int mTexName;
    public int mWidth = 1;
    public int mHeight = 1;
    private OnFrameAvailableListener mListener;
    private boolean mReleased;

    public SurfaceTexture(int texName) {
        mTexName = texName;
    }

    public SurfaceTexture(int texName, boolean singleBufferMode) {
        mTexName = texName;
    }

    public SurfaceTexture(boolean singleBufferMode) {
        mTexName = 0;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener) {
        mListener = listener;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener, Handler handler) {
        mListener = listener;
    }

    public void setDefaultBufferSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    public void updateTexImage() {}

    public void releaseTexImage() {}

    public void getTransformMatrix(float[] mtx) {
        for (int i = 0; i < 16; i++) mtx[i] = (i % 5 == 0) ? 1f : 0f;
    }

    public long getTimestamp() {
        return 0;
    }

    public void attachToGLContext(int texName) {}

    public void detachFromGLContext() {}

    public void release() {
        mReleased = true;
    }

    public boolean isReleased() {
        return mReleased;
    }
}
