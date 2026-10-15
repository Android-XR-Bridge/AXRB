package java.util;

/** HashMap already keeps insertion order; this adds access order and eviction. */
public class LinkedHashMap<K, V> extends HashMap<K, V> implements Map<K, V> {
    private final boolean accessOrder;

    public LinkedHashMap() {
        super();
        accessOrder = false;
    }

    public LinkedHashMap(int initialCapacity) {
        super(initialCapacity);
        accessOrder = false;
    }

    public LinkedHashMap(int initialCapacity, float loadFactor) {
        super(initialCapacity, loadFactor);
        accessOrder = false;
    }

    public LinkedHashMap(int initialCapacity, float loadFactor, boolean accessOrder) {
        super(initialCapacity, loadFactor);
        this.accessOrder = accessOrder;
    }

    public LinkedHashMap(Map<? extends K, ? extends V> m) {
        super();
        accessOrder = false;
        putAll(m);
    }

    @Override
    void afterAccess(Node<K, V> e) {
        if (accessOrder) moveToLast(e);
    }

    @Override
    void afterInsertion() {
        Node<K, V> first = head;
        if (first != null && removeEldestEntry(first)) removeNode(first.key);
    }

    protected boolean removeEldestEntry(Map.Entry<K, V> eldest) {
        return false;
    }
}
