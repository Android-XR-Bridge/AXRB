package java.lang;

public final class Byte extends Number implements Comparable<Byte> {
    public static final byte MIN_VALUE = -128;
    public static final byte MAX_VALUE = 127;
    public static final int SIZE = 8;
    public static final int BYTES = 1;
    @SuppressWarnings("unchecked")
    public static final Class<Byte> TYPE = (Class<Byte>) Class.getPrimitiveClass("byte");

    private static final Byte[] CACHE = new Byte[256];

    private final byte value;

    public Byte(byte value) {
        this.value = value;
    }

    public static Byte valueOf(byte b) {
        Byte c = CACHE[b + 128];
        if (c == null) {
            c = new Byte(b);
            CACHE[b + 128] = c;
        }
        return c;
    }

    public static Byte valueOf(String s) throws NumberFormatException {
        return valueOf(parseByte(s));
    }

    public static byte parseByte(String s) throws NumberFormatException {
        return parseByte(s, 10);
    }

    public static byte parseByte(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("s == null");
        return (byte) Long.parseLongRange(s, radix, MIN_VALUE, MAX_VALUE);
    }

    public static String toString(byte b) {
        return Integer.toString(b);
    }

    public static int compare(byte x, byte y) {
        return x - y;
    }

    public static int hashCode(byte value) {
        return value;
    }

    public static int toUnsignedInt(byte x) {
        return x & 0xff;
    }

    public static long toUnsignedLong(byte x) {
        return x & 0xffL;
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
        return value;
    }

    @Override
    public int compareTo(Byte other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Byte && ((Byte) o).value == value;
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
