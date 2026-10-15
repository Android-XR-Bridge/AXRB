package java.lang;

import java.util.Random;

public final class Math {
    public static final double E = 2.718281828459045;
    public static final double PI = 3.141592653589793;

    private static Random random;

    private Math() {}

    public static native double sin(double a);

    public static native double cos(double a);

    public static native double tan(double a);

    public static native double asin(double a);

    public static native double acos(double a);

    public static native double atan(double a);

    public static native double atan2(double y, double x);

    public static native double sinh(double x);

    public static native double cosh(double x);

    public static native double tanh(double x);

    public static native double exp(double a);

    public static native double expm1(double x);

    public static native double log(double a);

    public static native double log10(double a);

    public static native double log1p(double x);

    public static native double sqrt(double a);

    public static native double cbrt(double a);

    public static native double pow(double a, double b);

    public static native double hypot(double x, double y);

    public static native double IEEEremainder(double f1, double f2);

    public static native double ceil(double a);

    public static native double floor(double a);

    public static native double rint(double a);

    public static double toRadians(double deg) {
        return deg / 180.0 * PI;
    }

    public static double toDegrees(double rad) {
        return rad * 180.0 / PI;
    }

    public static int round(float a) {
        if (a != a) return 0;
        return (int) floor(a + 0.5d);
    }

    public static long round(double a) {
        if (a != a) return 0;
        // floor(a + 0.5) is wrong for 0.49999999999999994 and large odd values.
        if (a == (long) a) return (long) a;
        return (long) floor(a + 0.5d);
    }

    public static synchronized double random() {
        if (random == null) random = new Random();
        return random.nextDouble();
    }

    public static int abs(int a) {
        return a < 0 ? -a : a;
    }

    public static long abs(long a) {
        return a < 0 ? -a : a;
    }

    public static float abs(float a) {
        return Float.intBitsToFloat(Float.floatToRawIntBits(a) & 0x7fffffff);
    }

    public static double abs(double a) {
        return Double.longBitsToDouble(Double.doubleToRawLongBits(a) & 0x7fffffffffffffffL);
    }

    public static int max(int a, int b) {
        return a >= b ? a : b;
    }

    public static long max(long a, long b) {
        return a >= b ? a : b;
    }

    public static float max(float a, float b) {
        if (a != a) return a;
        if (a == 0.0f && b == 0.0f && Float.floatToRawIntBits(a) == Float.floatToRawIntBits(-0.0f)) return b;
        return a >= b ? a : b;
    }

    public static double max(double a, double b) {
        if (a != a) return a;
        if (a == 0.0d && b == 0.0d && Double.doubleToRawLongBits(a) == Double.doubleToRawLongBits(-0.0d)) return b;
        return a >= b ? a : b;
    }

    public static int min(int a, int b) {
        return a <= b ? a : b;
    }

    public static long min(long a, long b) {
        return a <= b ? a : b;
    }

    public static float min(float a, float b) {
        if (a != a) return a;
        if (a == 0.0f && b == 0.0f && Float.floatToRawIntBits(b) == Float.floatToRawIntBits(-0.0f)) return b;
        return a <= b ? a : b;
    }

    public static double min(double a, double b) {
        if (a != a) return a;
        if (a == 0.0d && b == 0.0d && Double.doubleToRawLongBits(b) == Double.doubleToRawLongBits(-0.0d)) return b;
        return a <= b ? a : b;
    }

    public static double signum(double d) {
        return (d == 0.0 || d != d) ? d : (d > 0 ? 1.0 : -1.0);
    }

    public static float signum(float f) {
        return (f == 0.0f || f != f) ? f : (f > 0 ? 1.0f : -1.0f);
    }

    public static int addExact(int x, int y) {
        int r = x + y;
        if (((x ^ r) & (y ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }

    public static long addExact(long x, long y) {
        long r = x + y;
        if (((x ^ r) & (y ^ r)) < 0) throw new ArithmeticException("long overflow");
        return r;
    }

    public static int subtractExact(int x, int y) {
        int r = x - y;
        if (((x ^ y) & (x ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }

    public static long subtractExact(long x, long y) {
        long r = x - y;
        if (((x ^ y) & (x ^ r)) < 0) throw new ArithmeticException("long overflow");
        return r;
    }

    public static int multiplyExact(int x, int y) {
        long r = (long) x * (long) y;
        if ((int) r != r) throw new ArithmeticException("integer overflow");
        return (int) r;
    }

    public static long multiplyExact(long x, long y) {
        long r = x * y;
        long ax = abs(x), ay = abs(y);
        if (((ax | ay) >>> 31 != 0)) {
            if (((y != 0) && (r / y != x)) || (x == Long.MIN_VALUE && y == -1)) {
                throw new ArithmeticException("long overflow");
            }
        }
        return r;
    }

    public static int toIntExact(long value) {
        if ((int) value != value) throw new ArithmeticException("integer overflow");
        return (int) value;
    }

    public static int negateExact(int a) {
        if (a == Integer.MIN_VALUE) throw new ArithmeticException("integer overflow");
        return -a;
    }

    public static int floorDiv(int x, int y) {
        int r = x / y;
        if ((x ^ y) < 0 && (r * y != x)) r--;
        return r;
    }

    public static long floorDiv(long x, long y) {
        long r = x / y;
        if ((x ^ y) < 0 && (r * y != x)) r--;
        return r;
    }

    public static int floorMod(int x, int y) {
        return x - floorDiv(x, y) * y;
    }

    public static long floorMod(long x, long y) {
        return x - floorDiv(x, y) * y;
    }

    public static double ulp(double d) {
        if (d != d) return d;
        if (Double.isInfinite(d)) return Double.POSITIVE_INFINITY;
        double a = abs(d);
        if (a == Double.MAX_VALUE) return 0x1.0p971;
        return Double.longBitsToDouble(Double.doubleToRawLongBits(a) + 1) - a;
    }

    public static float ulp(float f) {
        if (f != f) return f;
        if (Float.isInfinite(f)) return Float.POSITIVE_INFINITY;
        float a = abs(f);
        if (a == Float.MAX_VALUE) return 0x1.0p104f;
        return Float.intBitsToFloat(Float.floatToRawIntBits(a) + 1) - a;
    }
}
