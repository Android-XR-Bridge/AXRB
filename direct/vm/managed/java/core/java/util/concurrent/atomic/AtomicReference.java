package java.util.concurrent.atomic;

/** Lock-based: every access is synchronized on this object. */
public class AtomicReference<V> implements java.io.Serializable {
    private V value;

    public AtomicReference() {}

    public AtomicReference(V initialValue) {
        value = initialValue;
    }

    public final synchronized V get() {
        return value;
    }

    public final synchronized void set(V newValue) {
        value = newValue;
    }

    public final void lazySet(V newValue) {
        set(newValue);
    }

    public final synchronized boolean compareAndSet(V expect, V update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final synchronized V getAndSet(V newValue) {
        V old = value;
        value = newValue;
        return old;
    }

    public String toString() {
        return String.valueOf(get());
    }
}
