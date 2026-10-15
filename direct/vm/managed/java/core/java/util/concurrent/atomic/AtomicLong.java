package java.util.concurrent.atomic;

/** Lock-based: every access is synchronized on this object. */
public class AtomicLong extends Number implements java.io.Serializable {
    private long value;

    public AtomicLong() {}

    public AtomicLong(long initialValue) {
        value = initialValue;
    }

    public final synchronized long get() {
        return value;
    }

    public final synchronized void set(long newValue) {
        value = newValue;
    }

    public final void lazySet(long newValue) {
        set(newValue);
    }

    public final synchronized long getAndSet(long newValue) {
        long old = value;
        value = newValue;
        return old;
    }

    public final synchronized boolean compareAndSet(long expect, long update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final synchronized long getAndIncrement() {
        return value++;
    }

    public final synchronized long getAndDecrement() {
        return value--;
    }

    public final synchronized long getAndAdd(long delta) {
        long old = value;
        value += delta;
        return old;
    }

    public final synchronized long incrementAndGet() {
        return ++value;
    }

    public final synchronized long decrementAndGet() {
        return --value;
    }

    public final synchronized long addAndGet(long delta) {
        return value += delta;
    }

    public String toString() {
        return String.valueOf(get());
    }

    public int intValue() {
        return (int) get();
    }

    public long longValue() {
        return get();
    }

    public float floatValue() {
        return (float) get();
    }

    public double doubleValue() {
        return (double) get();
    }
}
