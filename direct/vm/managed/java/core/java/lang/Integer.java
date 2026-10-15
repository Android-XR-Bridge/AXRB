package java.lang;

public final class Integer extends Number implements Comparable<Integer> {
    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    @SuppressWarnings("unchecked")
    public static final Class<Integer> TYPE = (Class<Integer>) Class.getPrimitiveClass("int");

    private static final Integer[] CACHE = new Integer[256];

    private final int value;

    public Integer(int value) {
        this.value = value;
    }

    public Integer(String s) throws NumberFormatException {
        this.value = parseInt(s, 10);
    }

    public static Integer valueOf(int i) {
        if (i >= -128 && i <= 127) {
            Integer c = CACHE[i + 128];
            if (c == null) {
                c = new Integer(i);
                CACHE[i + 128] = c;
            }
            return c;
        }
        return new Integer(i);
    }

    public static Integer valueOf(String s) throws NumberFormatException {
        return valueOf(parseInt(s, 10));
    }

    public static Integer valueOf(String s, int radix) throws NumberFormatException {
        return valueOf(parseInt(s, radix));
    }

    public static int parseInt(String s) throws NumberFormatException {
        return parseInt(s, 10);
    }

    public static int parseInt(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        long v = Long.parseLongRange(s, radix, MIN_VALUE, MAX_VALUE);
        return (int) v;
    }

    public static int parseUnsignedInt(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        if (s.startsWith("-")) throw new NumberFormatException("Illegal leading minus sign on unsigned string " + s);
        long v = Long.parseLongRange(s, radix, 0, 0xffffffffL);
        return (int) v;
    }

    public static int parseUnsignedInt(String s) throws NumberFormatException {
        return parseUnsignedInt(s, 10);
    }

    public static Integer decode(String nm) throws NumberFormatException {
        return valueOf((int) Long.decodeRange(nm, MIN_VALUE, MAX_VALUE));
    }

    public static String toString(int i) {
        return Long.toString(i, 10);
    }

    public static String toString(int i, int radix) {
        return Long.toString(i, radix);
    }

    public static String toUnsignedString(int i) {
        return Long.toString(i & 0xffffffffL);
    }

    public static String toUnsignedString(int i, int radix) {
        return Long.toString(i & 0xffffffffL, radix);
    }

    public static String toHexString(int i) {
        return Long.toUnsignedString0(i & 0xffffffffL, 4);
    }

    public static String toOctalString(int i) {
        return Long.toUnsignedString0(i & 0xffffffffL, 3);
    }

    public static String toBinaryString(int i) {
        return Long.toUnsignedString0(i & 0xffffffffL, 1);
    }

    public static int compare(int x, int y) {
        return x < y ? -1 : (x == y ? 0 : 1);
    }

    public static int compareUnsigned(int x, int y) {
        return compare(x + MIN_VALUE, y + MIN_VALUE);
    }

    public static int divideUnsigned(int dividend, int divisor) {
        return (int) ((dividend & 0xffffffffL) / (divisor & 0xffffffffL));
    }

    public static int remainderUnsigned(int dividend, int divisor) {
        return (int) ((dividend & 0xffffffffL) % (divisor & 0xffffffffL));
    }

    public static long toUnsignedLong(int x) {
        return x & 0xffffffffL;
    }

    public static int signum(int i) {
        return (i >> 31) | (-i >>> 31);
    }

    public static int abs(int i) {
        return i < 0 ? -i : i;
    }

    public static int max(int a, int b) {
        return a >= b ? a : b;
    }

    public static int min(int a, int b) {
        return a <= b ? a : b;
    }

    public static int sum(int a, int b) {
        return a + b;
    }

    public static int hashCode(int value) {
        return value;
    }

    public static int bitCount(int i) {
        i = i - ((i >>> 1) & 0x55555555);
        i = (i & 0x33333333) + ((i >>> 2) & 0x33333333);
        i = (i + (i >>> 4)) & 0x0f0f0f0f;
        i = i + (i >>> 8);
        i = i + (i >>> 16);
        return i & 0x3f;
    }

    public static int highestOneBit(int i) {
        return i & (MIN_VALUE >>> numberOfLeadingZeros(i));
    }

    public static int lowestOneBit(int i) {
        return i & -i;
    }

    public static int numberOfLeadingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while ((i & MIN_VALUE) == 0) {
            n++;
            i <<= 1;
        }
        return n;
    }

    public static int numberOfTrailingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while ((i & 1) == 0) {
            n++;
            i >>>= 1;
        }
        return n;
    }

    public static int rotateLeft(int i, int distance) {
        return (i << distance) | (i >>> -distance);
    }

    public static int rotateRight(int i, int distance) {
        return (i >>> distance) | (i << -distance);
    }

    public static int reverse(int i) {
        int r = 0;
        for (int k = 0; k < 32; k++) {
            r = (r << 1) | (i & 1);
            i >>>= 1;
        }
        return r;
    }

    public static int reverseBytes(int i) {
        return (i << 24) | ((i & 0xff00) << 8) | ((i >>> 8) & 0xff00) | (i >>> 24);
    }

    public static Integer getInteger(String nm) {
        return getInteger(nm, null);
    }

    public static Integer getInteger(String nm, Integer val) {
        String v = nm == null || nm.isEmpty() ? null : System.getProperty(nm);
        if (v != null) {
            try {
                return decode(v);
            } catch (NumberFormatException e) {
                // fall through to the default, as the JDK does
            }
        }
        return val;
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
    public byte byteValue() {
        return (byte) value;
    }

    @Override
    public short shortValue() {
        return (short) value;
    }

    @Override
    public int compareTo(Integer other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Integer && ((Integer) o).value == value;
    }

    @Override
    public int hashCode() {
        return value;
    }

    @Override
    public String toString() {
        return toString(value);
    }
}
