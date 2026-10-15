package java.util.concurrent.atomic;

/** Lock-based: every access is synchronized on this object. */
public class AtomicBoolean implements java.io.Serializable {
    private boolean value;

    public AtomicBoolean() {}

    public AtomicBoolean(boolean initialValue) {
        value = initialValue;
    }

    public final synchronized boolean get() {
        return value;
    }

    public final synchronized void set(boolean newValue) {
        value = newValue;
    }

    public final void lazySet(boolean newValue) {
        set(newValue);
    }

    public final synchronized boolean compareAndSet(boolean expect, boolean update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final synchronized boolean getAndSet(boolean newValue) {
        boolean old = value;
        value = newValue;
        return old;
    }

    public String toString() {
        return Boolean.toString(get());
    }
}
