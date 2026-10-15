package android.util;

public class DisplayMetrics {
    public static final int DENSITY_LOW = 120;
    public static final int DENSITY_MEDIUM = 160;
    public static final int DENSITY_DEFAULT = DENSITY_MEDIUM;
    public static final int DENSITY_HIGH = 240;
    public static final int DENSITY_XHIGH = 320;
    public static final int DENSITY_XXHIGH = 480;
    public static final int DENSITY_XXXHIGH = 640;
    public static final float DENSITY_DEFAULT_SCALE = 1.0f / DENSITY_DEFAULT;
    /** The density of the host display the session renders to. */
    public static final int DENSITY_DEVICE_STABLE = nativeDeviceDensity();

    private static native int nativeDeviceDensity();

    public int widthPixels;
    public int heightPixels;
    public float density;
    public int densityDpi;
    public float scaledDensity;
    public float xdpi;
    public float ydpi;

    public DisplayMetrics() {}

    public void setTo(DisplayMetrics o) {
        widthPixels = o.widthPixels;
        heightPixels = o.heightPixels;
        density = o.density;
        densityDpi = o.densityDpi;
        scaledDensity = o.scaledDensity;
        xdpi = o.xdpi;
        ydpi = o.ydpi;
    }

    public void setToDefaults() {
        widthPixels = 0;
        heightPixels = 0;
        density = 1.0f;
        densityDpi = DENSITY_DEFAULT;
        scaledDensity = density;
        xdpi = DENSITY_DEFAULT;
        ydpi = DENSITY_DEFAULT;
    }

    public boolean equals(Object o) {
        if (!(o instanceof DisplayMetrics)) return false;
        DisplayMetrics d = (DisplayMetrics) o;
        return widthPixels == d.widthPixels && heightPixels == d.heightPixels && density == d.density
                && densityDpi == d.densityDpi && scaledDensity == d.scaledDensity && xdpi == d.xdpi && ydpi == d.ydpi;
    }

    public int hashCode() {
        return widthPixels * heightPixels * densityDpi;
    }

    public String toString() {
        return "DisplayMetrics{density=" + density + ", width=" + widthPixels + ", height=" + heightPixels
                + ", scaledDensity=" + scaledDensity + ", xdpi=" + xdpi + ", ydpi=" + ydpi + "}";
    }
}
