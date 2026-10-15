package java.lang;

/**
 * A Java thread backed by a host thread. `vmThread` is non-zero exactly while
 * the thread is alive; the runtime clears it and notifies waiters on this object
 * when the thread terminates (join() waits on that).
 *
 * Thread groups, priorities and stack sizes are recorded but have no scheduling
 * effect.
 */
public class Thread implements Runnable {
    public static final int MIN_PRIORITY = 1;
    public static final int NORM_PRIORITY = 5;
    public static final int MAX_PRIORITY = 10;

    private static int threadNumber;
    private static long nextId = 1;
    /**
     * Android's default handler (RuntimeInit's KillApplicationHandler) always
     * exists, and applications chain to it. Here it hands the exception back to
     * the runtime, which reports it and ends the process.
     */
    private static final class KillApplicationHandler implements UncaughtExceptionHandler {
        public void uncaughtException(Thread t, Throwable e) {
            Thread.<RuntimeException>rethrow(e);
        }
    }

    @SuppressWarnings("unchecked")
    private static <T extends Throwable> void rethrow(Throwable e) throws T {
        throw (T) e;
    }

    private static volatile UncaughtExceptionHandler defaultUncaughtHandler = new KillApplicationHandler();

    private volatile long vmThread;
    private volatile String name;
    private boolean daemon;
    private int priority = NORM_PRIORITY;
    private final Runnable target;
    private final long tid;
    private boolean started;
    private volatile UncaughtExceptionHandler uncaughtHandler;
    private ClassLoader contextClassLoader;

    /** Values of ThreadLocals, keyed by the ThreadLocal's identity. */
    java.util.HashMap<ThreadLocal<?>, Object> threadLocals;

    public interface UncaughtExceptionHandler {
        void uncaughtException(Thread t, Throwable e);
    }

    public enum State { NEW, RUNNABLE, BLOCKED, WAITING, TIMED_WAITING, TERMINATED }

    public Thread() {
        this((Runnable) null, (String) null);
    }

    public Thread(Runnable target) {
        this(target, (String) null);
    }

    public Thread(String name) {
        this((Runnable) null, name);
    }

    public Thread(Runnable target, String name) {
        this.target = target;
        this.name = name != null ? name : "Thread-" + nextThreadNumber();
        this.tid = nextThreadId();
        Thread parent = currentThread();
        if (parent != null) {
            this.daemon = parent.daemon;
            this.priority = parent.priority;
            this.contextClassLoader = parent.contextClassLoader;
        }
    }

    public Thread(ThreadGroup group, Runnable target, String name) {
        this(target, name);
    }

    public Thread(ThreadGroup group, Runnable target, String name, long stackSize) {
        this(target, name);
    }

    public Thread(ThreadGroup group, Runnable target) {
        this(target, (String) null);
    }

    public Thread(ThreadGroup group, String name) {
        this((Runnable) null, name);
    }

    /** Runtime-only: the peer of an attached host thread (already running). */
    private Thread(String name, long vmThread, boolean daemon) {
        this.target = null;
        this.name = name;
        this.vmThread = vmThread;
        this.daemon = daemon;
        this.tid = nextThreadId();
        this.started = true;
    }

    private static synchronized int nextThreadNumber() {
        return threadNumber++;
    }

    private static synchronized long nextThreadId() {
        return nextId++;
    }

    public static native Thread currentThread();

    public static native void yield();

    public static void sleep(long millis) throws InterruptedException {
        sleep(millis, 0);
    }

    public static void sleep(long millis, int nanos) throws InterruptedException {
        if (millis < 0) throw new IllegalArgumentException("millis < 0: " + millis);
        if (nanos < 0 || nanos > 999999) throw new IllegalArgumentException("nanos out of range: " + nanos);
        nativeSleep(millis, nanos);
    }

    private static native void nativeSleep(long millis, int nanos) throws InterruptedException;

    public static native boolean interrupted();

    public static native boolean holdsLock(Object obj);

    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        nativeStart();
    }

    /** Creates the host thread; it runs run() and then dispatches any uncaught exception. */
    private native void nativeStart();

    @Override
    public void run() {
        if (target != null) target.run();
    }

    public native void interrupt();

    public native boolean isInterrupted();

    public final boolean isAlive() {
        return vmThread != 0;
    }

    /** Every thread is in the one "main" group; null once terminated, like the JDK. */
    public final ThreadGroup getThreadGroup() {
        return getState() == State.TERMINATED ? null : ThreadGroup.MAIN;
    }

    public final void join() throws InterruptedException {
        synchronized (this) {
            while (isAlive()) wait();
        }
    }

    public final void join(long millis) throws InterruptedException {
        if (millis < 0) throw new IllegalArgumentException("timeout value is negative");
        if (millis == 0) {
            join();
            return;
        }
        synchronized (this) {
            long deadline = System.nanoTime() / 1000000L + millis;
            while (isAlive()) {
                long left = deadline - System.nanoTime() / 1000000L;
                if (left <= 0) break;
                wait(left);
            }
        }
    }

    public final String getName() {
        return name;
    }

    public final synchronized void setName(String name) {
        if (name == null) throw new NullPointerException("name == null");
        this.name = name;
    }

    public long getId() {
        return tid;
    }

    public final boolean isDaemon() {
        return daemon;
    }

    public final void setDaemon(boolean on) {
        if (started) throw new IllegalThreadStateException();
        daemon = on;
    }

    public final int getPriority() {
        return priority;
    }

    public final void setPriority(int newPriority) {
        if (newPriority < MIN_PRIORITY || newPriority > MAX_PRIORITY) throw new IllegalArgumentException();
        priority = newPriority;
    }

    public State getState() {
        if (!started) return State.NEW;
        return isAlive() ? State.RUNNABLE : State.TERMINATED;
    }

    public ClassLoader getContextClassLoader() {
        return contextClassLoader;
    }

    public void setContextClassLoader(ClassLoader cl) {
        contextClassLoader = cl;
    }

    public StackTraceElement[] getStackTrace() {
        if (this != currentThread()) return new StackTraceElement[0];
        StackTraceElement[] t = new Throwable().getStackTrace();
        // Drop this frame.
        if (t.length == 0) return t;
        StackTraceElement[] out = new StackTraceElement[t.length - 1];
        System.arraycopy(t, 1, out, 0, out.length);
        return out;
    }

    public static void setDefaultUncaughtExceptionHandler(UncaughtExceptionHandler eh) {
        defaultUncaughtHandler = eh;
    }

    public static UncaughtExceptionHandler getDefaultUncaughtExceptionHandler() {
        return defaultUncaughtHandler;
    }

    public UncaughtExceptionHandler getUncaughtExceptionHandler() {
        return uncaughtHandler;
    }

    public void setUncaughtExceptionHandler(UncaughtExceptionHandler eh) {
        uncaughtHandler = eh;
    }

    /**
     * Called by the runtime when run() throws. Returns true if a handler took
     * it; false means nobody did and the runtime treats it as fatal, as
     * Android's default handler does.
     */
    final boolean dispatchUncaughtException(Throwable e) {
        UncaughtExceptionHandler h = uncaughtHandler;
        if (h == null) h = defaultUncaughtHandler;
        if (h == null || h instanceof KillApplicationHandler) return false;
        h.uncaughtException(this, e);
        return true;
    }

    @Override
    public String toString() {
        return "Thread[" + name + "," + priority + ",main]";
    }
}
