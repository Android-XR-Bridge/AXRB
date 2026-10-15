package java.lang;

import java.util.function.Supplier;

/**
 * Values live in a per-thread map owned by the Thread, so a thread's values
 * die with it. Unlike OpenJDK, entries are not weakly keyed: a ThreadLocal
 * that becomes unreachable stays in the maps of threads that set it until
 * those threads end or call remove().
 */
public class ThreadLocal<T> {
    public ThreadLocal() {}

    protected T initialValue() {
        return null;
    }

    public static <S> ThreadLocal<S> withInitial(final Supplier<? extends S> supplier) {
        if (supplier == null) throw new NullPointerException();
        return new ThreadLocal<S>() {
            @Override
            protected S initialValue() {
                return supplier.get();
            }
        };
    }

    @SuppressWarnings("unchecked")
    public T get() {
        Thread t = Thread.currentThread();
        java.util.HashMap<ThreadLocal<?>, Object> map = t.threadLocals;
        if (map != null && map.containsKey(this)) return (T) map.get(this);
        T v = initialValue();
        if (map == null) map = t.threadLocals = new java.util.HashMap<ThreadLocal<?>, Object>();
        map.put(this, v);
        return v;
    }

    public void set(T value) {
        Thread t = Thread.currentThread();
        if (t.threadLocals == null) t.threadLocals = new java.util.HashMap<ThreadLocal<?>, Object>();
        t.threadLocals.put(this, value);
    }

    public void remove() {
        Thread t = Thread.currentThread();
        if (t.threadLocals != null) t.threadLocals.remove(this);
    }

    // Identity semantics: a HashMap key must not depend on subclass equals().
    @Override
    public final int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public final boolean equals(Object o) {
        return this == o;
    }
}
