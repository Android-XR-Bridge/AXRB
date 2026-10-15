package android.os;

public final class Looper {
    private static final ThreadLocal<Looper> THREAD_LOOPER = new ThreadLocal<Looper>();
    private static Looper mainLooper;

    final MessageQueue queue;
    final Thread thread;

    private Looper(boolean quitAllowed) {
        queue = new MessageQueue(quitAllowed);
        thread = Thread.currentThread();
    }

    public static void prepare() {
        prepare(true);
    }

    private static void prepare(boolean quitAllowed) {
        if (THREAD_LOOPER.get() != null) throw new RuntimeException("Only one Looper may be created per thread");
        THREAD_LOOPER.set(new Looper(quitAllowed));
    }

    public static void prepareMainLooper() {
        prepare(false);
        synchronized (Looper.class) {
            if (mainLooper != null) throw new IllegalStateException("The main Looper has already been prepared.");
            mainLooper = myLooper();
        }
    }

    public static Looper getMainLooper() {
        synchronized (Looper.class) {
            return mainLooper;
        }
    }

    public static Looper myLooper() {
        return THREAD_LOOPER.get();
    }

    public static MessageQueue myQueue() {
        Looper l = myLooper();
        if (l == null) throw new IllegalStateException("No Looper; Looper.prepare() wasn't called on this thread.");
        return l.queue;
    }

    public static void loop() {
        final Looper me = myLooper();
        if (me == null) throw new RuntimeException("No Looper; Looper.prepare() wasn't called on this thread.");
        for (;;) {
            Message msg = me.queue.next();
            if (msg == null) return;  // quitting
            msg.target.dispatchMessage(msg);
            msg.recycle();
        }
    }

    public boolean isCurrentThread() {
        return Thread.currentThread() == thread;
    }

    public Thread getThread() {
        return thread;
    }

    public MessageQueue getQueue() {
        return queue;
    }

    public void quit() {
        queue.quit(false);
    }

    public void quitSafely() {
        queue.quit(true);
    }

    /**
     * Runtime-internal (@hide on Android): ends the main thread's loop when the
     * desktop session closes. Android never quits the main looper; its process
     * is killed instead, which is what ending the loop leads to here.
     */
    public static void shutdownMainLooperForSessionEnd() {
        Looper main = getMainLooper();
        if (main != null) main.queue.shutdown();
    }

    @Override
    public String toString() {
        return "Looper (" + thread.getName() + ", tid " + thread.getId() + ") {" + Integer.toHexString(System.identityHashCode(this)) + "}";
    }
}
