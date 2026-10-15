package android.os;

public final class SystemClock {
    private SystemClock() {}

    /** Milliseconds since boot, not counting deep sleep (a monotonic host clock). */
    public static native long uptimeMillis();

    public static long elapsedRealtime() {
        return uptimeMillis();
    }

    public static long elapsedRealtimeNanos() {
        return System.nanoTime();
    }

    public static long currentThreadTimeMillis() {
        return uptimeMillis();
    }

    public static void sleep(long ms) {
        long start = uptimeMillis();
        long duration = ms;
        boolean interrupted = false;
        do {
            try {
                Thread.sleep(duration);
            } catch (InterruptedException e) {
                interrupted = true;
            }
            duration = start + ms - uptimeMillis();
        } while (duration > 0);
        if (interrupted) Thread.currentThread().interrupt();
    }
}
