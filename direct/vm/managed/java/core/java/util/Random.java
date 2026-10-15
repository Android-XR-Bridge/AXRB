package java.util;

/**
 * The linear congruential generator java.util.Random is specified to be (the
 * sequence for a given seed is part of the API). Unseeded instances take their
 * seed from the host CSPRNG (spec 11); this class is still not suitable for
 * security purposes, exactly as on Android.
 */
public class Random implements java.io.Serializable {
    private static final long MULTIPLIER = 0x5DEECE66DL;
    private static final long ADDEND = 0xBL;
    private static final long MASK = (1L << 48) - 1;

    private long seed;
    private double nextNextGaussian;
    private boolean haveNextNextGaussian;

    public Random() {
        this(nativeEntropySeed());
    }

    public Random(long seed) {
        setSeed(seed);
    }

    private static native long nativeEntropySeed();

    public synchronized void setSeed(long seed) {
        this.seed = (seed ^ MULTIPLIER) & MASK;
        haveNextNextGaussian = false;
    }

    protected synchronized int next(int bits) {
        seed = (seed * MULTIPLIER + ADDEND) & MASK;
        return (int) (seed >>> (48 - bits));
    }

    public void nextBytes(byte[] bytes) {
        for (int i = 0, len = bytes.length; i < len; ) {
            for (int rnd = nextInt(), n = Math.min(len - i, 4); n-- > 0; rnd >>= 8) bytes[i++] = (byte) rnd;
        }
    }

    public int nextInt() {
        return next(32);
    }

    public int nextInt(int bound) {
        if (bound <= 0) throw new IllegalArgumentException("bound must be positive");
        int r = next(31);
        int m = bound - 1;
        if ((bound & m) == 0) {
            r = (int) ((bound * (long) r) >> 31);
        } else {
            for (int u = r; u - (r = u % bound) + m < 0; u = next(31)) {}
        }
        return r;
    }

    public long nextLong() {
        return ((long) next(32) << 32) + next(32);
    }

    public boolean nextBoolean() {
        return next(1) != 0;
    }

    public float nextFloat() {
        return next(24) / ((float) (1 << 24));
    }

    public double nextDouble() {
        return (((long) next(26) << 27) + next(27)) * 0x1.0p-53;
    }

    public synchronized double nextGaussian() {
        if (haveNextNextGaussian) {
            haveNextNextGaussian = false;
            return nextNextGaussian;
        }
        double v1, v2, s;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            s = v1 * v1 + v2 * v2;
        } while (s >= 1 || s == 0);
        double multiplier = Math.sqrt(-2 * Math.log(s) / s);
        nextNextGaussian = v2 * multiplier;
        haveNextNextGaussian = true;
        return v1 * multiplier;
    }
}
