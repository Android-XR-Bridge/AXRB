package java.lang;

public final class Short extends Number implements Comparable<Short> {
    public static final short MIN_VALUE = -32768;
    public static final short MAX_VALUE = 32767;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    @SuppressWarnings("unchecked")
    public static final Class<Short> TYPE = (Class<Short>) Class.getPrimitiveClass("short");

    private static final Short[] CACHE = new Short[256];

    private final short value;

    public Short(short value) {
        this.value = value;
    }

    public static Short valueOf(short s) {
        if (s >= -128 && s <= 127) {
            Short c = CACHE[s + 128];
            if (c == null) {
                c = new Short(s);
                CACHE[s + 128] = c;
            }
            return c;
        }
        return new Short(s);
    }

    public static Short valueOf(String s) throws NumberFormatException {
        return valueOf(parseShort(s));
    }

    public static short parseShort(String s) throws NumberFormatException {
        return parseShort(s, 10);
    }

    public static short parseShort(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        return (short) Long.parseLongRange(s, radix, MIN_VALUE, MAX_VALUE);
    }

    public static String toString(short s) {
        return Integer.toString(s);
    }

    public static int compare(short x, short y) {
        return x - y;
    }

    public static int hashCode(short value) {
        return value;
    }

    public static short reverseBytes(short i) {
        return (short) (((i & 0xff00) >> 8) | (i << 8));
    }

    public static int toUnsignedInt(short x) {
        return x & 0xffff;
    }

    @Override
    public int intValue() {
        return value;
    }

    @Override
    public long longValue() {
        return value;
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
    public short shortValue() {
        return value;
    }

    @Override
    public int compareTo(Short other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Short && ((Short) o).value == value;
    }

    @Override
    public int hashCode() {
        return value;
    }

    @Override
    public String toString() {
        return Integer.toString(value);
    }
}
