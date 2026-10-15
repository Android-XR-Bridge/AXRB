package android.media;

import android.os.Handler;
import android.view.Surface;

public class ImageReader implements AutoCloseable {
    public interface OnImageAvailableListener {
        void onImageAvailable(ImageReader reader);
    }

    private final int mWidth;
    private final int mHeight;
    private final int mFormat;
    private final int mMaxImages;
    private final Surface mSurface = new Surface();

    protected ImageReader(int width, int height, int format, int maxImages) {
        mWidth = width;
        mHeight = height;
        mFormat = format;
        mMaxImages = maxImages;
    }

    public static ImageReader newInstance(int width, int height, int format, int maxImages) {
        return new ImageReader(width, height, format, maxImages);
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public int getImageFormat() {
        return mFormat;
    }

    public int getMaxImages() {
        return mMaxImages;
    }

    public Surface getSurface() {
        return mSurface;
    }

    public Image acquireLatestImage() {
        return null;
    }

    public Image acquireNextImage() {
        return null;
    }

    public void setOnImageAvailableListener(OnImageAvailableListener listener, Handler handler) {}

    public void close() {
        mSurface.release();
    }
}
