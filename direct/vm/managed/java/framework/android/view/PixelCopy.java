package android.view;

import android.graphics.Bitmap;
import android.os.Handler;

public final class PixelCopy {
    public static final int SUCCESS = 0;
    public static final int ERROR_UNKNOWN = 1;
    public static final int ERROR_TIMEOUT = 2;
    public static final int ERROR_SOURCE_NO_DATA = 3;
    public static final int ERROR_SOURCE_INVALID = 4;
    public static final int ERROR_DESTINATION_INVALID = 5;

    public interface OnPixelCopyFinishedListener {
        void onPixelCopyFinished(int copyResult);
    }

    private PixelCopy() {}

    public static void request(SurfaceView source, Bitmap dest, final OnPixelCopyFinishedListener listener,
            Handler listenerThread) {
        request(source.getHolder().getSurface(), dest, listener, listenerThread);
    }

    public static void request(Surface source, Bitmap dest, final OnPixelCopyFinishedListener listener,
            Handler listenerThread) {
        listenerThread.post(new Runnable() {
            public void run() {
                listener.onPixelCopyFinished(ERROR_SOURCE_NO_DATA);
            }
        });
    }

    public static void request(Window source, Bitmap dest, OnPixelCopyFinishedListener listener,
            Handler listenerThread) {
        request((Surface) null, dest, listener, listenerThread);
    }
}
