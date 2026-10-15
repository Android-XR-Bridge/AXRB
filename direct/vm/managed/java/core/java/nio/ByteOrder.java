package java.nio;

public final class ByteOrder {
    public static final ByteOrder BIG_ENDIAN = new ByteOrder("BIG_ENDIAN");
    public static final ByteOrder LITTLE_ENDIAN = new ByteOrder("LITTLE_ENDIAN");

    private final String name;

    private ByteOrder(String name) {
        this.name = name;
    }

    public static ByteOrder nativeOrder() {
        return LITTLE_ENDIAN;
    }

    public String toString() {
        return name;
    }
}
