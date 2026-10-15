package android.view;

import android.content.res.Resources;
import android.graphics.Point;
import android.graphics.Rect;
import android.util.DisplayMetrics;

/**
 * The runner's single logical display: the host window's client area at the
 * configured density. Its size follows the host window.
 */
public final class Display {
    public static final int DEFAULT_DISPLAY = 0;
    public static final int INVALID_DISPLAY = -1;
    public static final int STATE_UNKNOWN = 0;
    public static final int STATE_OFF = 1;
    public static final int STATE_ON = 2;
    public static final int FLAG_SECURE = 1 << 1;

    private static Display sDefault;

    private Display() {}

    public static Display getDefault() {
        synchronized (Display.class) {
            if (sDefault == null) sDefault = new Display();
            return sDefault;
        }
    }

    public int getDisplayId() {
        return DEFAULT_DISPLAY;
    }

    public String getName() {
        return "Built-in Screen";
    }

    public boolean isValid() {
        return true;
    }

    public int getFlags() {
        return 0;
    }

    public int getState() {
        return STATE_ON;
    }

    public void getSize(Point outSize) {
        DisplayMetrics m = Resources.getSystem().getDisplayMetrics();
        outSize.x = m.widthPixels;
        outSize.y = m.heightPixels;
    }

    public void getRealSize(Point outSize) {
        getSize(outSize);
    }

    public void getRectSize(Rect outSize) {
        DisplayMetrics m = Resources.getSystem().getDisplayMetrics();
        outSize.set(0, 0, m.widthPixels, m.heightPixels);
    }

    public void getCurrentSizeRange(Point outSmallestSize, Point outLargestSize) {
        DisplayMetrics m = Resources.getSystem().getDisplayMetrics();
        int small = Math.min(m.widthPixels, m.heightPixels);
        int large = Math.max(m.widthPixels, m.heightPixels);
        outSmallestSize.x = small;
        outSmallestSize.y = small;
        outLargestSize.x = large;
        outLargestSize.y = large;
    }

    @Deprecated
    public int getWidth() {
        return Resources.getSystem().getDisplayMetrics().widthPixels;
    }

    @Deprecated
    public int getHeight() {
        return Resources.getSystem().getDisplayMetrics().heightPixels;
    }

    public int getRotation() {
        return Surface.ROTATION_0;
    }

    @Deprecated
    public int getOrientation() {
        return getRotation();
    }

    public float getRefreshRate() {
        return getMode().getRefreshRate();
    }

    public static final class Mode {
        private final int mModeId;
        private final int mWidth;
        private final int mHeight;
        private final float mRefreshRate;

        public Mode(int width, int height, float refreshRate) {
            this(1, width, height, refreshRate);
        }

        Mode(int modeId, int width, int height, float refreshRate) {
            mModeId = modeId;
            mWidth = width;
            mHeight = height;
            mRefreshRate = refreshRate;
        }

        public int getModeId() {
            return mModeId;
        }

        public int getPhysicalWidth() {
            return mWidth;
        }

        public int getPhysicalHeight() {
            return mHeight;
        }

        public float getRefreshRate() {
            return mRefreshRate;
        }

        public float[] getAlternativeRefreshRates() {
            return new float[0];
        }

        public String toString() {
            return "{id=" + mModeId + ", width=" + mWidth + ", height=" + mHeight + ", fps=" + mRefreshRate + "}";
        }
    }

    /** The refresh rate the host reports for the headset (or monitor). */
    static float sRefreshRate = 90.0f;

    public Mode getMode() {
        DisplayMetrics m = Resources.getSystem().getDisplayMetrics();
        return new Mode(1, m.widthPixels, m.heightPixels, sRefreshRate);
    }

    public Mode[] getSupportedModes() {
        return new Mode[] {getMode()};
    }

    public float[] getSupportedRefreshRates() {
        return new float[] {sRefreshRate};
    }

    public long getAppVsyncOffsetNanos() {
        return 0;
    }

    public long getPresentationDeadlineNanos() {
        return (long) (1000000000L / sRefreshRate) - 1000000L;
    }

    public android.view.DisplayCutout getCutout() {
        return null;
    }

    public boolean isHdr() {
        return false;
    }

    public boolean isWideColorGamut() {
        return false;
    }

    public void getMetrics(DisplayMetrics outMetrics) {
        outMetrics.setTo(Resources.getSystem().getDisplayMetrics());
    }

    public void getRealMetrics(DisplayMetrics outMetrics) {
        getMetrics(outMetrics);
    }

    public int getPixelFormat() {
        return android.graphics.PixelFormat.RGBA_8888;
    }

    @Override
    public String toString() {
        DisplayMetrics m = Resources.getSystem().getDisplayMetrics();
        return "Display id " + DEFAULT_DISPLAY + ": " + m.widthPixels + "x" + m.heightPixels + ", " + m.densityDpi
                + " dpi";
    }
}
