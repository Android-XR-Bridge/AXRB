package java.util.concurrent.atomic;

/** Lock-based: every access is synchronized on this object. */
public class AtomicInteger extends Number implements java.io.Serializable {
    private int value;

    public AtomicInteger() {}

    public AtomicInteger(int initialValue) {
        value = initialValue;
    }

    public final synchronized int get() {
        return value;
    }

    public final synchronized void set(int newValue) {
        value = newValue;
    }

    public final void lazySet(int newValue) {
        set(newValue);
    }

    public final synchronized int getAndSet(int newValue) {
        int old = value;
        value = newValue;
        return old;
    }

    public final synchronized boolean compareAndSet(int expect, int update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final synchronized int getAndIncrement() {
        return value++;
    }

    public final synchronized int getAndDecrement() {
        return value--;
    }

    public final synchronized int getAndAdd(int delta) {
        int old = value;
        value += delta;
        return old;
    }

    public final synchronized int incrementAndGet() {
        return ++value;
    }

    public final synchronized int decrementAndGet() {
        return --value;
    }

    public final synchronized int addAndGet(int delta) {
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
