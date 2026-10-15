/** Minimal managed-code milestone; it must run on real x86_64 ART. */
public final class NativeArt {
    private static native long nativeRoundTrip(long value);
    private static long callback(long value) { return value * 7; }
    private static final Object lock = new Object();
    private static int total;
    private static volatile Throwable failure;
    private static final ThreadLocal<Integer> local = new ThreadLocal<Integer>();
    public static void main(String[] args) throws Exception {
        System.load("/data/local/tmp/x64-probe/libnative-art-probe.so");
        if (nativeRoundTrip(1234) != 8643) throw new AssertionError("JNI callback");
        long sum = 0;
        for (int i = 0; i < 10000; ++i) sum += i;
        if (sum != 49995000L) throw new AssertionError("arithmetic");
        if (!"Dalvik".equals(System.getProperty("java.vm.name")))
            throw new AssertionError("Expected Android ART");
        Thread[] workers = new Thread[4];
        for (int id = 0; id < workers.length; id++) {
            final int value = id;
            workers[id] = new Thread(new Runnable() {
                public void run() {
                    try {
                        local.set(value);
                        for (int n = 0; n < 5000; n++) {
                            if (local.get() != value || nativeRoundTrip(n) != n * 7L + 5)
                                throw new AssertionError("thread TLS/JNI");
                            synchronized (lock) { total++; }
                        }
                    } catch (Throwable error) { failure = error; }
                }
            });
            workers[id].start();
        }
        System.gc(); // Also collect while worker threads can hold JNI/local roots.
        for (Thread worker : workers) worker.join();
        if (failure != null) throw new AssertionError("worker", failure);
        if (total != 20000) throw new AssertionError("monitor");
        byte[][] retained = new byte[64][];
        for (int round = 0; round < 8; round++) {
            for (int i = 0; i < retained.length; i++) {
                retained[i] = new byte[32768]; retained[i][123] = (byte)i;
            }
            System.gc();
            for (int i = 0; i < retained.length; i++)
                if (retained[i][123] != (byte)i) throw new AssertionError("GC contents");
        }
        try { throw new IllegalArgumentException("expected"); }
        catch (IllegalArgumentException expected) {
            if (!"expected".equals(expected.getMessage())) throw new AssertionError("exception");
        }
        System.out.println("NATIVE_X64_ART_PASS arithmetic=" + sum + " threads=" + total + " JNI callbacks and GC");
    }
}
