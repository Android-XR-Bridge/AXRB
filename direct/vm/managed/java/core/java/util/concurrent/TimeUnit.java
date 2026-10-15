package java.util.concurrent;

public enum TimeUnit {
    NANOSECONDS(1L),
    MICROSECONDS(1000L),
    MILLISECONDS(1000000L),
    SECONDS(1000000000L),
    MINUTES(60000000000L),
    HOURS(3600000000000L),
    DAYS(86400000000000L);

    private final long nanos;

    TimeUnit(long nanos) {
        this.nanos = nanos;
    }

    private static long scale(long d, long num, long den) {
        if (num >= den) {
            long m = num / den;
            if (d > Long.MAX_VALUE / m) return Long.MAX_VALUE;
            if (d < Long.MIN_VALUE / m) return Long.MIN_VALUE;
            return d * m;
        }
        return d / (den / num);
    }

    public long convert(long sourceDuration, TimeUnit sourceUnit) {
        return scale(sourceDuration, sourceUnit.nanos, nanos);
    }

    public long toNanos(long d) {
        return scale(d, nanos, 1L);
    }

    public long toMicros(long d) {
        return scale(d, nanos, 1000L);
    }

    public long toMillis(long d) {
        return scale(d, nanos, 1000000L);
    }

    public long toSeconds(long d) {
        return scale(d, nanos, 1000000000L);
    }

    public long toMinutes(long d) {
        return scale(d, nanos, 60000000000L);
    }

    public long toHours(long d) {
        return scale(d, nanos, 3600000000000L);
    }

    public long toDays(long d) {
        return scale(d, nanos, 86400000000000L);
    }

    public void sleep(long timeout) throws InterruptedException {
        if (timeout > 0) {
            long ns = toNanos(timeout);
            Thread.sleep(ns / 1000000L, (int) (ns % 1000000L));
        }
    }

    public void timedJoin(Thread thread, long timeout) throws InterruptedException {
        if (timeout > 0) thread.join(Math.max(1L, toMillis(timeout)));
    }

    public void timedWait(Object obj, long timeout) throws InterruptedException {
        if (timeout > 0) obj.wait(Math.max(1L, toMillis(timeout)));
    }
}
