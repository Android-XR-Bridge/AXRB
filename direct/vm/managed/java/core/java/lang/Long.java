package java.lang;

public final class Long extends Number implements Comparable<Long> {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    @SuppressWarnings("unchecked")
    public static final Class<Long> TYPE = (Class<Long>) Class.getPrimitiveClass("long");

    private static final Long[] CACHE = new Long[256];
    private static final char[] DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz".toCharArray();

    private final long value;

    public Long(long value) {
        this.value = value;
    }

    public Long(String s) throws NumberFormatException {
        this.value = parseLong(s, 10);
    }

    public static Long valueOf(long l) {
        if (l >= -128 && l <= 127) {
            int i = (int) l + 128;
            Long c = CACHE[i];
            if (c == null) {
                c = new Long(l);
                CACHE[i] = c;
            }
            return c;
        }
        return new Long(l);
    }

    public static Long valueOf(String s) throws NumberFormatException {
        return valueOf(parseLong(s, 10));
    }

    public static Long valueOf(String s, int radix) throws NumberFormatException {
        return valueOf(parseLong(s, radix));
    }

    public static long parseLong(String s) throws NumberFormatException {
        return parseLong(s, 10);
    }

    public static long parseLong(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        return parseLongRange(s, radix, MIN_VALUE, MAX_VALUE);
    }

    public static long parseUnsignedLong(String s) throws NumberFormatException {
        return parseUnsignedLong(s, 10);
    }

    public static long parseUnsignedLong(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        if (s.isEmpty() || s.charAt(0) == '-') throw NumberFormatException.forInputString(s);
        int start = s.charAt(0) == '+' ? 1 : 0;
        if (start == s.length()) throw NumberFormatException.forInputString(s);
        long result = 0;
        for (int i = start; i < s.length(); i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw NumberFormatException.forInputString(s);
            long hi = unsignedMultiplyHigh(result, radix);
            long lo = result * radix;
            long next = lo + d;
            if (hi != 0 || compareUnsigned(next, lo) < 0) {
                throw new NumberFormatException("String value " + s + " exceeds range of unsigned long.");
            }
            result = next;
        }
        return result;
    }

    private static long unsignedMultiplyHigh(long x, long y) {
        long x0 = x & 0xffffffffL, x1 = x >>> 32;
        long y0 = y & 0xffffffffL, y1 = y >>> 32;
        long w0 = x0 * y0;
        long t = x1 * y0 + (w0 >>> 32);
        long w1 = (t & 0xffffffffL) + x0 * y1;
        return x1 * y1 + (t >>> 32) + (w1 >>> 32);
    }

    /** Parses a signed integer in `radix` and checks it lies in [min, max]. */
    static long parseLongRange(String s, int radix, long min, long max) throws NumberFormatException {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) {
            throw new NumberFormatException("Invalid radix: " + radix);
        }
        int n = s.length();
        if (n == 0) throw NumberFormatException.forInputString(s);
        int i = 0;
        boolean negative = false;
        char first = s.charAt(0);
        if (first == '-' || first == '+') {
            negative = first == '-';
            i = 1;
            if (n == 1) throw NumberFormatException.forInputString(s);
        }
        // Accumulate negatively so MIN_VALUE is representable.
        long limit = negative ? min : -max;
        long result = 0;
        long multmin = limit / radix;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0 || result < multmin) throw NumberFormatException.forInputString(s);
            result *= radix;
            if (result < limit + d) throw NumberFormatException.forInputString(s);
            result -= d;
        }
        return negative ? result : -result;
    }

    static long decodeRange(String nm, long min, long max) throws NumberFormatException {
        if (nm.isEmpty()) throw new NumberFormatException("Zero length string");
        int index = 0;
        boolean negative = false;
        char first = nm.charAt(0);
        if (first == '-' || first == '+') {
            negative = first == '-';
            index++;
        }
        int radix = 10;
        if (nm.startsWith("0x", index) || nm.startsWith("0X", index)) {
            index += 2;
            radix = 16;
        } else if (nm.startsWith("#", index)) {
            index++;
            radix = 16;
        } else if (nm.startsWith("0", index) && nm.length() > 1 + index) {
            index++;
            radix = 8;
        }
        if (nm.startsWith("-", index) || nm.startsWith("+", index)) {
            throw new NumberFormatException("Sign character in wrong position");
        }
        return parseLongRange((negative ? "-" : "") + nm.substring(index), radix, min, max);
    }

    public static Long decode(String nm) throws NumberFormatException {
        return valueOf(decodeRange(nm, MIN_VALUE, MAX_VALUE));
    }

    public static String toString(long i) {
        return toString(i, 10);
    }

    public static String toString(long i, int radix) {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) radix = 10;
        if (i == 0) return "0";
        char[] buf = new char[65];
        int pos = 65;
        boolean negative = i < 0;
        if (!negative) i = -i;
        while (i != 0) {
            buf[--pos] = DIGITS[(int) -(i % radix)];
            i /= radix;
        }
        if (negative) buf[--pos] = '-';
        return new String(buf, pos, 65 - pos);
    }

    public static String toUnsignedString(long i) {
        return toUnsignedString(i, 10);
    }

    public static String toUnsignedString(long i, int radix) {
        if (i >= 0) return toString(i, radix);
        if (radix == 16) return toUnsignedString0(i, 4);
        if (radix == 8) return toUnsignedString0(i, 3);
        if (radix == 2) return toUnsignedString0(i, 1);
        long quotient = (i >>> 1) / radix << 1;
        long rem = i - quotient * radix;
        if (rem >= radix) {
            rem -= radix;
            quotient++;
        }
        return toString(quotient, radix) + DIGITS[(int) rem];
    }

    /** Unsigned conversion for power-of-two radixes (shift = log2(radix)). */
    static String toUnsignedString0(long i, int shift) {
        char[] buf = new char[64];
        int pos = 64;
        int radix = 1 << shift;
        long mask = radix - 1;
        do {
            buf[--pos] = DIGITS[(int) (i & mask)];
            i >>>= shift;
        } while (i != 0);
        return new String(buf, pos, 64 - pos);
    }

    public static String toHexString(long i) {
        return toUnsignedString0(i, 4);
    }

    public static String toOctalString(long i) {
        return toUnsignedString0(i, 3);
    }

    public static String toBinaryString(long i) {
        return toUnsignedString0(i, 1);
    }

    public static int compare(long x, long y) {
        return x < y ? -1 : (x == y ? 0 : 1);
    }

    public static int compareUnsigned(long x, long y) {
        return compare(x + MIN_VALUE, y + MIN_VALUE);
    }

    public static long divideUnsigned(long dividend, long divisor) {
        if (divisor < 0) return compareUnsigned(dividend, divisor) < 0 ? 0 : 1;
        if (dividend >= 0) return dividend / divisor;
        long q = ((dividend >>> 1) / divisor) << 1;
        long r = dividend - q * divisor;
        return q + (compareUnsigned(r, divisor) >= 0 ? 1 : 0);
    }

    public static long remainderUnsigned(long dividend, long divisor) {
        return dividend - divideUnsigned(dividend, divisor) * divisor;
    }

    public static int signum(long i) {
        return (int) ((i >> 63) | (-i >>> 63));
    }

    public static long abs(long i) {
        return i < 0 ? -i : i;
    }

    public static long max(long a, long b) {
        return a >= b ? a : b;
    }

    public static long min(long a, long b) {
        return a <= b ? a : b;
    }

    public static long sum(long a, long b) {
        return a + b;
    }

    public static int hashCode(long value) {
        return (int) (value ^ (value >>> 32));
    }

    public static int bitCount(long i) {
        return Integer.bitCount((int) i) + Integer.bitCount((int) (i >>> 32));
    }

    public static int numberOfLeadingZeros(long i) {
        int hi = (int) (i >>> 32);
        return hi != 0 ? Integer.numberOfLeadingZeros(hi) : 32 + Integer.numberOfLeadingZeros((int) i);
    }

    public static int numberOfTrailingZeros(long i) {
        int lo = (int) i;
        return lo != 0 ? Integer.numberOfTrailingZeros(lo) : 32 + Integer.numberOfTrailingZeros((int) (i >>> 32));
    }

    public static long highestOneBit(long i) {
        return i & (MIN_VALUE >>> numberOfLeadingZeros(i));
    }

    public static long lowestOneBit(long i) {
        return i & -i;
    }

    public static long rotateLeft(long i, int distance) {
        return (i << distance) | (i >>> -distance);
    }

    public static long rotateRight(long i, int distance) {
        return (i >>> distance) | (i << -distance);
    }

    public static long reverse(long i) {
        return ((long) Integer.reverse((int) i) << 32) | (Integer.reverse((int) (i >>> 32)) & 0xffffffffL);
    }

    public static long reverseBytes(long i) {
        return ((long) Integer.reverseBytes((int) i) << 32) | (Integer.reverseBytes((int) (i >>> 32)) & 0xffffffffL);
    }

    @Override
    public int intValue() {
        return (int) value;
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
    public int compareTo(Long other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Long && ((Long) o).value == value;
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
