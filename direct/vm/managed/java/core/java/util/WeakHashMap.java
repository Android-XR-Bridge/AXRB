package java.util;

/**
 * Holds its keys strongly: entries are only removed explicitly. Callers use
 * WeakHashMap as a cache keyed by objects that live for the whole session, so
 * the difference is a bounded leak rather than a behaviour change.
 */
public class WeakHashMap<K, V> extends HashMap<K, V> {
    public WeakHashMap() {
        super();
    }

    public WeakHashMap(int initialCapacity) {
        super(initialCapacity);
    }

    public WeakHashMap(int initialCapacity, float loadFactor) {
        super(initialCapacity, loadFactor);
    }

    public WeakHashMap(Map<? extends K, ? extends V> m) {
        super(m);
    }
}
