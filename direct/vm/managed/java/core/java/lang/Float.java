package java.lang;

public final class Float extends Number implements Comparable<Float> {
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f;
    public static final float NEGATIVE_INFINITY = -1.0f / 0.0f;
    public static final float NaN = 0.0f / 0.0f;
    public static final float MAX_VALUE = 0x1.fffffeP+127f;
    public static final float MIN_NORMAL = 0x1.0p-126f;
    public static final float MIN_VALUE = 0x0.000002P-126f;
    public static final int MAX_EXPONENT = 127;
    public static final int MIN_EXPONENT = -126;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    @SuppressWarnings("unchecked")
    public static final Class<Float> TYPE = (Class<Float>) Class.getPrimitiveClass("float");

    private final float value;

    public Float(float value) {
        this.value = value;
    }

    public Float(double value) {
        this.value = (float) value;
    }

    public Float(String s) throws NumberFormatException {
        this(parseFloat(s));
    }

    public static Float valueOf(float f) {
        return new Float(f);
    }

    public static Float valueOf(String s) throws NumberFormatException {
        return new Float(parseFloat(s));
    }

    /** Shortest decimal that round-trips to this float (not to the double). */
    public static String toString(float f) {
        return Double.nativeToString(f, true);
    }

    public static float parseFloat(String s) throws NumberFormatException {
        if (s == null) throw new NullPointerException("s == null");
        return nativeParse(s.trim());
    }

    private static native float nativeParse(String s) throws NumberFormatException;

    public static boolean isNaN(float v) {
        return v != v;
    }

    public static boolean isInfinite(float v) {
        return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY;
    }

    public static boolean isFinite(float f) {
        return Math.abs(f) <= MAX_VALUE;
    }

    public static native int floatToRawIntBits(float value);

    public static int floatToIntBits(float value) {
        return isNaN(value) ? 0x7fc00000 : floatToRawIntBits(value);
    }

    public static native float intBitsToFloat(int bits);

    public static int compare(float f1, float f2) {
        if (f1 < f2) return -1;
        if (f1 > f2) return 1;
        int b1 = floatToIntBits(f1), b2 = floatToIntBits(f2);
        return b1 == b2 ? 0 : (b1 < b2 ? -1 : 1);
    }

    public static int hashCode(float value) {
        return floatToIntBits(value);
    }

    public static float sum(float a, float b) {
        return a + b;
    }

    public static float max(float a, float b) {
        return Math.max(a, b);
    }

    public static float min(float a, float b) {
        return Math.min(a, b);
    }

    public boolean isNaN() {
        return isNaN(value);
    }

    public boolean isInfinite() {
        return isInfinite(value);
    }

    @Override
    public int intValue() {
        return (int) value;
    }

    @Override
    public long longValue() {
        return (long) value;
    }

    @Override
    public float floatValue() {
        return value;
    }

    @Override
    public double doubleValue() {
        return value;
    }

    @Override
    public int compareTo(Float other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Float && floatToIntBits(((Float) o).value) == floatToIntBits(value);
    }

    @Override
    public int hashCode() {
        return hashCode(value);
    }

    @Override
    public String toString() {
        return toString(value);
    }
}
