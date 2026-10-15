package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

public interface SurfaceHolder {
    int SURFACE_TYPE_NORMAL = 0;
    int SURFACE_TYPE_HARDWARE = 1;
    int SURFACE_TYPE_GPU = 2;
    int SURFACE_TYPE_PUSH_BUFFERS = 3;

    interface Callback {
        void surfaceCreated(SurfaceHolder holder);

        void surfaceChanged(SurfaceHolder holder, int format, int width, int height);

        void surfaceDestroyed(SurfaceHolder holder);
    }

    interface Callback2 extends Callback {
        void surfaceRedrawNeeded(SurfaceHolder holder);
    }

    void addCallback(Callback callback);

    void removeCallback(Callback callback);

    boolean isCreating();

    void setType(int type);

    void setFixedSize(int width, int height);

    void setSizeFromLayout();

    void setFormat(int format);

    void setKeepScreenOn(boolean screenOn);

    Canvas lockCanvas();

    Canvas lockCanvas(Rect dirty);

    void unlockCanvasAndPost(Canvas canvas);

    Rect getSurfaceFrame();

    Surface getSurface();
}
