package android.content.res;

import android.util.DisplayMetrics;
import java.util.Locale;

/** The host display the session renders to (window client area or headless surface). */
final class ResourcesHost {
    private ResourcesHost() {}

    static DisplayMetrics displayMetrics() {
        int[] info = new int[3];
        nativeDisplayInfo(info);
        DisplayMetrics m = new DisplayMetrics();
        m.widthPixels = info[0];
        m.heightPixels = info[1];
        m.densityDpi = info[2];
        m.density = info[2] / (float) DisplayMetrics.DENSITY_DEFAULT;
        m.scaledDensity = m.density;
        m.xdpi = info[2];
        m.ydpi = info[2];
        return m;
    }

    static Configuration configuration() {
        DisplayMetrics m = displayMetrics();
        Configuration c = new Configuration();
        c.locale = Locale.getDefault();
        c.densityDpi = m.densityDpi;
        c.screenWidthDp = (int) (m.widthPixels / m.density);
        c.screenHeightDp = (int) (m.heightPixels / m.density);
        c.smallestScreenWidthDp = Math.min(c.screenWidthDp, c.screenHeightDp);
        c.orientation = m.widthPixels > m.heightPixels ? Configuration.ORIENTATION_LANDSCAPE
                : Configuration.ORIENTATION_PORTRAIT;
        // Screen size class and aspect, as WindowManagerService computes them.
        int longDp = Math.max(c.screenWidthDp, c.screenHeightDp);
        int shortDp = Math.min(c.screenWidthDp, c.screenHeightDp);
        int size;
        if (longDp >= 960 && shortDp >= 720) {
            size = Configuration.SCREENLAYOUT_SIZE_XLARGE;
        } else if (longDp >= 640 && shortDp >= 480) {
            size = Configuration.SCREENLAYOUT_SIZE_LARGE;
        } else if (longDp >= 470 && shortDp >= 320) {
            size = Configuration.SCREENLAYOUT_SIZE_NORMAL;
        } else {
            size = Configuration.SCREENLAYOUT_SIZE_SMALL;
        }
        int aspect = (longDp * 3) / 5 >= (shortDp - 1) ? Configuration.SCREENLAYOUT_LONG_YES
                : Configuration.SCREENLAYOUT_LONG_NO;
        c.screenLayout = size | aspect | Configuration.SCREENLAYOUT_LAYOUTDIR_LTR;
        return c;
    }

    /** out = {widthPixels, heightPixels, densityDpi}. */
    private static native void nativeDisplayInfo(int[] out);
}
