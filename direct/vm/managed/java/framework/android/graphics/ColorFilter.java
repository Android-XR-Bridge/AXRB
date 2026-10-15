package android.graphics;

/** Base class for color filters (tints). */
public class ColorFilter {
    /** Hidden: the color `argb` after this filter (per-color filters only). */
    int filter(int argb) {
        return argb;
    }
}