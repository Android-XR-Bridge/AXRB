package android.graphics;

/** A tint: SRC_IN / SRC_ATOP replace the color and keep the alpha; others pass through. */
public class PorterDuffColorFilter extends ColorFilter {
    private final int mColor;
    private final PorterDuff.Mode mMode;

    public PorterDuffColorFilter(int color, PorterDuff.Mode mode) {
        mColor = color;
        mMode = mode;
    }

    public int getColor() {
        return mColor;
    }

    public PorterDuff.Mode getMode() {
        return mMode;
    }

    @Override
    int filter(int argb) {
        switch (mMode) {
            case SRC:
                return mColor;
            case SRC_IN:
            case SRC_ATOP: {
                int a = ((argb >>> 24) * (mColor >>> 24)) / 255;
                if (mMode == PorterDuff.Mode.SRC_ATOP) a = argb >>> 24;
                return (a << 24) | (mColor & 0xFFFFFF);
            }
            case MULTIPLY: {
                int a = ((argb >>> 24) * (mColor >>> 24)) / 255;
                int r = (((argb >> 16) & 0xff) * ((mColor >> 16) & 0xff)) / 255;
                int g = (((argb >> 8) & 0xff) * ((mColor >> 8) & 0xff)) / 255;
                int b = ((argb & 0xff) * (mColor & 0xff)) / 255;
                return (a << 24) | (r << 16) | (g << 8) | b;
            }
            default:
                return argb;
        }
    }
}