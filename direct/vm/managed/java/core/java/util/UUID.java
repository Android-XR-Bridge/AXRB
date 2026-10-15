package java.util;

public final class UUID implements java.io.Serializable, Comparable<UUID> {
    private final long mostSigBits;
    private final long leastSigBits;
    private static Random random;

    public UUID(long mostSigBits, long leastSigBits) {
        this.mostSigBits = mostSigBits;
        this.leastSigBits = leastSigBits;
    }

    public static synchronized UUID randomUUID() {
        if (random == null) random = new Random(System.nanoTime() ^ System.currentTimeMillis());
        long msb = (random.nextLong() & ~0xf000L) | 0x4000L;
        long lsb = (random.nextLong() & 0x3fffffffffffffffL) | 0x8000000000000000L;
        return new UUID(msb, lsb);
    }

    public static UUID nameUUIDFromBytes(byte[] name) {
        // Version 3 needs MD5; a stable hash keeps the identity deterministic.
        long msb = 0x9e3779b97f4a7c15L, lsb = 0xc2b2ae3d27d4eb4fL;
        for (byte b : name) {
            msb = (msb ^ (b & 0xff)) * 0x100000001b3L;
            lsb = (lsb ^ (b & 0xff)) * 0xff51afd7ed558ccdL;
        }
        return new UUID((msb & ~0xf000L) | 0x3000L, (lsb & 0x3fffffffffffffffL) | 0x8000000000000000L);
    }

    public static UUID fromString(String name) {
        String[] parts = name.split("-");
        if (parts.length != 5) throw new IllegalArgumentException("Invalid UUID string: " + name);
        long msb = (Long.parseLong(parts[0], 16) << 32) | (Long.parseLong(parts[1], 16) << 16) | Long.parseLong(parts[2], 16);
        long lsb = (Long.parseLong(parts[3], 16) << 48) | Long.parseLong(parts[4], 16);
        return new UUID(msb, lsb);
    }

    public long getMostSignificantBits() {
        return mostSigBits;
    }

    public long getLeastSignificantBits() {
        return leastSigBits;
    }

    public int version() {
        return (int) ((mostSigBits >> 12) & 0x0f);
    }

    public int variant() {
        return (int) ((leastSigBits >>> (64 - (leastSigBits >>> 62))) & (leastSigBits >> 63));
    }

    private static String hex(long value, int digits) {
        String s = Long.toHexString(value & ((1L << (digits * 4)) - 1) | (1L << (digits * 4)));
        return s.substring(1);
    }

    public String toString() {
        return hex(mostSigBits >> 32, 8) + "-" + hex(mostSigBits >> 16, 4) + "-" + hex(mostSigBits, 4) + "-"
                + hex(leastSigBits >> 48, 4) + "-" + hex(leastSigBits, 12);
    }

    public int hashCode() {
        long h = mostSigBits ^ leastSigBits;
        return ((int) (h >> 32)) ^ (int) h;
    }

    public boolean equals(Object obj) {
        if (!(obj instanceof UUID)) return false;
        UUID id = (UUID) obj;
        return mostSigBits == id.mostSigBits && leastSigBits == id.leastSigBits;
    }

    public int compareTo(UUID val) {
        return mostSigBits < val.mostSigBits ? -1 : mostSigBits > val.mostSigBits ? 1
                : leastSigBits < val.leastSigBits ? -1 : leastSigBits > val.leastSigBits ? 1 : 0;
    }
}
