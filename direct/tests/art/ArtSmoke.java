import java.lang.reflect.Method;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

/** Runs on unmodified Android ART, not AXRB's managed interpreter. */
public final class ArtSmoke {
    private static native int nativeRoundTrip(int[] values);
    public static int twice(int value) { return value * 2; }

    public static void main(String[] args) throws Exception {
        System.out.println("ART_SMOKE_START " + System.getProperty("java.vm.name"));
        Method method = ArtSmoke.class.getMethod("twice", int.class);
        if (!Integer.valueOf(42).equals(method.invoke(null, 21))) throw new AssertionError("reflection");
        System.load("/system/lib64/libaxrb_art_smoke.so");
        int[] values = {21};
        if (nativeRoundTrip(values) != 42 || values[0] != 42) throw new AssertionError("JNI callback/array/thread");
        AtomicInteger result = new AtomicInteger();
        CountDownLatch finished = new CountDownLatch(1);
        Thread worker = new Thread(() -> { result.set(42); finished.countDown(); }, "art-smoke");
        worker.start();
        finished.await();
        worker.join();
        if (result.get() != 42) throw new AssertionError("thread visibility");
        for (int i = 0; i < 10000; i++) {
            byte[] bytes = new byte[4096];
            bytes[i % bytes.length] = 42;
        }
        System.gc();
        try { throw new IllegalStateException("expected"); }
        catch (IllegalStateException expected) {
            if (!"expected".equals(expected.getMessage())) throw new AssertionError("exception");
        }
        System.out.println("ART_SMOKE_PASS");
    }
}
