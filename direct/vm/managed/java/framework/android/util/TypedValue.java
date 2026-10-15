package android.util;

/** A dynamically typed data value, as resources and compiled XML store it. */
public class TypedValue {
    public static final int TYPE_NULL = 0x00;
    public static final int TYPE_REFERENCE = 0x01;
    public static final int TYPE_ATTRIBUTE = 0x02;
    public static final int TYPE_STRING = 0x03;
    public static final int TYPE_FLOAT = 0x04;
    public static final int TYPE_DIMENSION = 0x05;
    public static final int TYPE_FRACTION = 0x06;
    public static final int TYPE_FIRST_INT = 0x10;
    public static final int TYPE_INT_DEC = 0x10;
    public static final int TYPE_INT_HEX = 0x11;
    public static final int TYPE_INT_BOOLEAN = 0x12;
    public static final int TYPE_FIRST_COLOR_INT = 0x1c;
    public static final int TYPE_INT_COLOR_ARGB8 = 0x1c;
    public static final int TYPE_INT_COLOR_RGB8 = 0x1d;
    public static final int TYPE_INT_COLOR_ARGB4 = 0x1e;
    public static final int TYPE_INT_COLOR_RGB4 = 0x1f;
    public static final int TYPE_LAST_COLOR_INT = 0x1f;
    public static final int TYPE_LAST_INT = 0x1f;

    public static final int COMPLEX_UNIT_SHIFT = 0;
    public static final int COMPLEX_UNIT_MASK = 0xf;
    public static final int COMPLEX_UNIT_PX = 0;
    public static final int COMPLEX_UNIT_DIP = 1;
    public static final int COMPLEX_UNIT_SP = 2;
    public static final int COMPLEX_UNIT_PT = 3;
    public static final int COMPLEX_UNIT_IN = 4;
    public static final int COMPLEX_UNIT_MM = 5;
    public static final int COMPLEX_UNIT_FRACTION = 0;
    public static final int COMPLEX_UNIT_FRACTION_PARENT = 1;
    public static final int COMPLEX_RADIX_SHIFT = 4;
    public static final int COMPLEX_RADIX_MASK = 0x3;
    public static final int COMPLEX_RADIX_23p0 = 0;
    public static final int COMPLEX_RADIX_16p7 = 1;
    public static final int COMPLEX_RADIX_8p15 = 2;
    public static final int COMPLEX_RADIX_0p23 = 3;
    public static final int COMPLEX_MANTISSA_SHIFT = 8;
    public static final int COMPLEX_MANTISSA_MASK = 0xffffff;

    public static final int DATA_NULL_UNDEFINED = 0;
    public static final int DATA_NULL_EMPTY = 1;
    public static final int DENSITY_DEFAULT = 0;
    public static final int DENSITY_NONE = 0xffff;

    public int type;
    public CharSequence string;
    public int data;
    public int assetCookie;
    public int resourceId;
    public int changingConfigurations = -1;
    public int density;
    public int sourceResourceId;

    private static final float MANTISSA_MULT = 1.0f / (1 << COMPLEX_MANTISSA_SHIFT);
    private static final float[] RADIX_MULTS = {1.0f * MANTISSA_MULT, 1.0f / (1 << 7) * MANTISSA_MULT,
            1.0f / (1 << 15) * MANTISSA_MULT, 1.0f / (1 << 23) * MANTISSA_MULT};

    public final float getFloat() {
        return Float.intBitsToFloat(data);
    }

    public boolean isColorType() {
        return type >= TYPE_FIRST_COLOR_INT && type <= TYPE_LAST_COLOR_INT;
    }

    public static float complexToFloat(int complex) {
        return (complex & (COMPLEX_MANTISSA_MASK << COMPLEX_MANTISSA_SHIFT))
                * RADIX_MULTS[(complex >> COMPLEX_RADIX_SHIFT) & COMPLEX_RADIX_MASK];
    }

    public static float complexToDimension(int data, DisplayMetrics metrics) {
        return applyDimension((data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK, complexToFloat(data), metrics);
    }

    public static int complexToDimensionPixelOffset(int data, DisplayMetrics metrics) {
        return (int) applyDimension((data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK, complexToFloat(data), metrics);
    }

    public static int complexToDimensionPixelSize(int data, DisplayMetrics metrics) {
        final float value = complexToFloat(data);
        final float f = applyDimension((data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK, value, metrics);
        final int res = (int) (f >= 0 ? f + 0.5f : f - 0.5f);
        if (res != 0) return res;
        if (value == 0) return 0;
        if (value > 0) return 1;
        return -1;
    }

    public static float complexToFraction(int data, float base, float pbase) {
        switch ((data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK) {
            case COMPLEX_UNIT_FRACTION:
                return complexToFloat(data) * base;
            case COMPLEX_UNIT_FRACTION_PARENT:
                return complexToFloat(data) * pbase;
        }
        return 0;
    }

    public static float applyDimension(int unit, float value, DisplayMetrics metrics) {
        switch (unit) {
            case COMPLEX_UNIT_PX:
                return value;
            case COMPLEX_UNIT_DIP:
                return value * metrics.density;
            case COMPLEX_UNIT_SP:
                return value * metrics.scaledDensity;
            case COMPLEX_UNIT_PT:
                return value * metrics.xdpi * (1.0f / 72);
            case COMPLEX_UNIT_IN:
                return value * metrics.xdpi;
            case COMPLEX_UNIT_MM:
                return value * metrics.xdpi * (1.0f / 25.4f);
        }
        return 0;
    }

    public float getDimension(DisplayMetrics metrics) {
        return complexToDimension(data, metrics);
    }

    public int getComplexUnit() {
        return COMPLEX_UNIT_MASK & (data >> COMPLEX_UNIT_SHIFT);
    }

    public float getFraction(float base, float pbase) {
        return complexToFraction(data, base, pbase);
    }

    public final CharSequence coerceToString() {
        int t = type;
        if (t == TYPE_STRING) return string;
        return coerceToString(t, data);
    }

    private static final String[] DIMENSION_UNIT_STRS = {"px", "dip", "sp", "pt", "in", "mm"};
    private static final String[] FRACTION_UNIT_STRS = {"%", "%p"};

    public static final String coerceToString(int type, int data) {
        switch (type) {
            case TYPE_NULL:
                return null;
            case TYPE_REFERENCE:
                return "@" + data;
            case TYPE_ATTRIBUTE:
                return "?" + data;
            case TYPE_FLOAT:
                return Float.toString(Float.intBitsToFloat(data));
            case TYPE_DIMENSION: {
                int unit = (data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK;
                String u = unit < DIMENSION_UNIT_STRS.length ? DIMENSION_UNIT_STRS[unit] : "?";
                return Float.toString(complexToFloat(data)) + u;
            }
            case TYPE_FRACTION: {
                int unit = (data >> COMPLEX_UNIT_SHIFT) & COMPLEX_UNIT_MASK;
                String u = unit < FRACTION_UNIT_STRS.length ? FRACTION_UNIT_STRS[unit] : "?";
                return Float.toString(complexToFloat(data) * 100) + u;
            }
            case TYPE_INT_HEX:
                return "0x" + Integer.toHexString(data);
            case TYPE_INT_BOOLEAN:
                return data != 0 ? "true" : "false";
        }
        if (type >= TYPE_FIRST_COLOR_INT && type <= TYPE_LAST_COLOR_INT) {
            return "#" + Integer.toHexString(data);
        } else if (type >= TYPE_FIRST_INT && type <= TYPE_LAST_INT) {
            return Integer.toString(data);
        }
        return null;
    }

    public void setTo(TypedValue other) {
        type = other.type;
        string = other.string;
        data = other.data;
        assetCookie = other.assetCookie;
        resourceId = other.resourceId;
        density = other.density;
        changingConfigurations = other.changingConfigurations;
        sourceResourceId = other.sourceResourceId;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append("TypedValue{t=0x").append(Integer.toHexString(type));
        sb.append("/d=0x").append(Integer.toHexString(data));
        if (type == TYPE_STRING) sb.append(" \"").append(string != null ? string : "<null>").append("\"");
        if (assetCookie != 0) sb.append(" a=").append(assetCookie);
        if (resourceId != 0) sb.append(" r=0x").append(Integer.toHexString(resourceId));
        sb.append("}");
        return sb.toString();
    }
}
