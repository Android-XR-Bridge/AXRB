package java.util.concurrent;

import java.util.HashMap;
import java.util.Map;

/** Coarse-locked; enough for the map's contract, not its scalability. */
public class ConcurrentHashMap<K, V> extends HashMap<K, V> implements ConcurrentMap<K, V> {
    public ConcurrentHashMap() {}

    public ConcurrentHashMap(int initialCapacity) {
        super(initialCapacity);
    }

    public ConcurrentHashMap(int initialCapacity, float loadFactor) {
        super(initialCapacity);
    }

    public ConcurrentHashMap(int initialCapacity, float loadFactor, int concurrencyLevel) {
        super(initialCapacity);
    }

    public ConcurrentHashMap(Map<? extends K, ? extends V> m) {
        super(m);
    }

    public synchronized V get(Object key) {
        return super.get(key);
    }

    public synchronized boolean containsKey(Object key) {
        return super.containsKey(key);
    }

    public synchronized V put(K key, V value) {
        if (key == null || value == null) throw new NullPointerException();
        return super.put(key, value);
    }

    public synchronized V remove(Object key) {
        return super.remove(key);
    }

    public synchronized int size() {
        return super.size();
    }

    public synchronized void clear() {
        super.clear();
    }

    public synchronized V putIfAbsent(K key, V value) {
        V old = super.get(key);
        if (old == null) super.put(key, value);
        return old;
    }

    public synchronized boolean remove(Object key, Object value) {
        V old = super.get(key);
        if (old == null || !old.equals(value)) return false;
        super.remove(key);
        return true;
    }

    public synchronized boolean replace(K key, V oldValue, V newValue) {
        V old = super.get(key);
        if (old == null || !old.equals(oldValue)) return false;
        super.put(key, newValue);
        return true;
    }

    public synchronized V replace(K key, V value) {
        return super.containsKey(key) ? super.put(key, value) : null;
    }
}
