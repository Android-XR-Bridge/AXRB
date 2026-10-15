package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.util.AttributeSet;
import java.util.ArrayList;

/**
 * A view owning a Surface. The surface exists while the view is attached and
 * visible; holders get surfaceCreated / surfaceChanged / surfaceDestroyed on
 * the UI thread, as on Android.
 */
public class SurfaceView extends View {
    private final ArrayList<SurfaceHolder.Callback> mCallbacks = new ArrayList<SurfaceHolder.Callback>();
    private final Surface mSurface = new Surface("SurfaceView", 0, 0);
    private boolean mSurfaceCreated;
    private boolean mCreating;
    private int mFormat = PixelFormat.RGB_565;
    private int mRequestedWidth = -1;
    private int mRequestedHeight = -1;
    private int mSurfaceWidth = -1;
    private int mSurfaceHeight = -1;
    private int mReportedFormat = -1;
    private final Rect mSurfaceFrame = new Rect();

    public SurfaceView(Context context) {
        this(context, null);
    }

    public SurfaceView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public SurfaceView(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public SurfaceView(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mSurface.release();
    }

    public SurfaceHolder getHolder() {
        return mHolder;
    }

    public void setZOrderOnTop(boolean onTop) {}

    public void setZOrderMediaOverlay(boolean isMediaOverlay) {}

    public void setSecure(boolean isSecure) {}

    /** Surfaces arrive a little after the window, as from SurfaceFlinger. */
    private static final long SURFACE_DELAY_MS = 50;

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        postDelayed(new Runnable() {
            public void run() {
                updateSurface();
            }
        }, SURFACE_DELAY_MS);
    }

    @Override
    protected void onDetachedFromWindow() {
        destroySurface();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        if (visibility == VISIBLE) {
            updateSurface();
        } else {
            destroySurface();
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateSurface();
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = mRequestedWidth >= 0
                ? resolveSizeAndState(mRequestedWidth, widthMeasureSpec, 0)
                : getDefaultSize(0, widthMeasureSpec);
        int height = mRequestedHeight >= 0
                ? resolveSizeAndState(mRequestedHeight, heightMeasureSpec, 0)
                : getDefaultSize(0, heightMeasureSpec);
        setMeasuredDimension(width, height);
    }

    private int surfaceWidth() {
        if (mRequestedWidth > 0) return mRequestedWidth;
        int w = getWidth();
        return w > 0 ? w : getResources().getDisplayMetrics().widthPixels;
    }

    private int surfaceHeight() {
        if (mRequestedHeight > 0) return mRequestedHeight;
        int h = getHeight();
        return h > 0 ? h : getResources().getDisplayMetrics().heightPixels;
    }

    private SurfaceHolder.Callback[] callbacks() {
        synchronized (mCallbacks) {
            return mCallbacks.toArray(new SurfaceHolder.Callback[mCallbacks.size()]);
        }
    }

    void updateSurface() {
        if (!isAttachedToWindow() || getWindowVisibility() != VISIBLE) {
            android.util.Log.d("SurfaceView", "updateSurface: attached=" + isAttachedToWindow() + " visibility="
                    + getWindowVisibility());
            return;
        }
        int w = surfaceWidth();
        int h = surfaceHeight();
        boolean created = false;
        if (!mSurfaceCreated) {
            Surface fresh = new Surface("SurfaceView", w, h);
            mSurface.mNativeObject = fresh.mNativeObject;
            mSurface.setSize(w, h);
            mSurfaceCreated = true;
            created = true;
            android.util.Log.i("SurfaceView", "surface created " + w + "x" + h + " for " + mCallbacks.size() + " callbacks");
            mCreating = true;
            for (SurfaceHolder.Callback c : callbacks()) c.surfaceCreated(mHolder);
        }
        if (created || w != mSurfaceWidth || h != mSurfaceHeight || mFormat != mReportedFormat) {
            mSurfaceWidth = w;
            mSurfaceHeight = h;
            mReportedFormat = mFormat;
            mSurface.setSize(w, h);
            mSurfaceFrame.set(0, 0, w, h);
            for (SurfaceHolder.Callback c : callbacks()) c.surfaceChanged(mHolder, mFormat, w, h);
            for (SurfaceHolder.Callback c : callbacks()) {
                if (c instanceof SurfaceHolder.Callback2) ((SurfaceHolder.Callback2) c).surfaceRedrawNeeded(mHolder);
            }
        }
        mCreating = false;
    }

    void destroySurface() {
        if (!mSurfaceCreated) return;
        for (SurfaceHolder.Callback c : callbacks()) c.surfaceDestroyed(mHolder);
        mSurfaceCreated = false;
        mSurface.release();
        mSurfaceWidth = -1;
        mSurfaceHeight = -1;
    }

    private final SurfaceHolder mHolder = new SurfaceHolder() {
        public void addCallback(Callback callback) {
            synchronized (mCallbacks) {
                if (!mCallbacks.contains(callback)) mCallbacks.add(callback);
            }
        }

        public void removeCallback(Callback callback) {
            synchronized (mCallbacks) {
                mCallbacks.remove(callback);
            }
        }

        public boolean isCreating() {
            return mCreating;
        }

        public void setType(int type) {}

        public void setFixedSize(int width, int height) {
            if (mRequestedWidth != width || mRequestedHeight != height) {
                mRequestedWidth = width;
                mRequestedHeight = height;
                requestLayout();
                updateSurface();
            }
        }

        public void setSizeFromLayout() {
            if (mRequestedWidth != -1 || mRequestedHeight != -1) {
                mRequestedWidth = mRequestedHeight = -1;
                requestLayout();
                updateSurface();
            }
        }

        public void setFormat(int format) {
            if (format == PixelFormat.OPAQUE) format = PixelFormat.RGB_565;
            mFormat = format;
            if (mSurfaceCreated) updateSurface();
        }

        public void setKeepScreenOn(boolean screenOn) {}

        public Canvas lockCanvas() {
            return null;
        }

        public Canvas lockCanvas(Rect dirty) {
            return null;
        }

        public void unlockCanvasAndPost(Canvas canvas) {}

        public Rect getSurfaceFrame() {
            return mSurfaceFrame;
        }

        public Surface getSurface() {
            return mSurface;
        }
    };
}
