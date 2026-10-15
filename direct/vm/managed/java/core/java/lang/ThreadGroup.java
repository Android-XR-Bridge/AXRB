package java.lang;

/** Present for constructor compatibility; groups have no effect in this runtime. */
public class ThreadGroup {
    static final ThreadGroup MAIN = new ThreadGroup(null, "main");
    private final String name;
    private final ThreadGroup parent;

    public ThreadGroup(String name) {
        this(null, name);
    }

    public ThreadGroup(ThreadGroup parent, String name) {
        this.parent = parent;
        this.name = name;
    }

    public final String getName() {
        return name;
    }

    public final ThreadGroup getParent() {
        return parent;
    }

    public int activeCount() {
        return 0;
    }

    public int activeGroupCount() {
        return 0;
    }

    public int enumerate(Thread[] list) {
        return 0;
    }

    public int enumerate(Thread[] list, boolean recurse) {
        return 0;
    }

    public int enumerate(ThreadGroup[] list) {
        return 0;
    }

    public int enumerate(ThreadGroup[] list, boolean recurse) {
        return 0;
    }

    public final int getMaxPriority() {
        return Thread.MAX_PRIORITY;
    }

    public final boolean isDaemon() {
        return false;
    }

    public void uncaughtException(Thread t, Throwable e) {
        Thread.UncaughtExceptionHandler h = Thread.getDefaultUncaughtExceptionHandler();
        if (h != null) h.uncaughtException(t, e);
        else e.printStackTrace();
    }
}
