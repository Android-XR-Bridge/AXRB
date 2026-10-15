package java.util;

/**
 * Separate chaining with a power-of-two table. Iteration order is insertion
 * order within LinkedHashMap and unspecified here. No treeification: a key
 * type with a pathological hashCode degrades to linear chains.
 */
public class HashMap<K, V> extends AbstractMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    static class Node<K, V> implements Map.Entry<K, V> {
        final int hash;
        final K key;
        V value;
        Node<K, V> next;
        // Doubly linked insertion/access order, maintained for every map so
        // LinkedHashMap needs no separate node type.
        Node<K, V> before;
        Node<K, V> after;

        Node(int hash, K key, V value, Node<K, V> next) {
            this.hash = hash;
            this.key = key;
            this.value = value;
            this.next = next;
        }

        public final K getKey() {
            return key;
        }

        public final V getValue() {
            return value;
        }

        public final V setValue(V newValue) {
            V old = value;
            value = newValue;
            return old;
        }

        public final boolean equals(Object o) {
            if (o == this) return true;
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue());
        }

        public final int hashCode() {
            return Objects.hashCode(key) ^ Objects.hashCode(value);
        }

        public final String toString() {
            return key + "=" + value;
        }
    }

    private Node<K, V>[] table;
    private int size;
    private int threshold;
    private final float loadFactor;
    transient int modCount;
    Node<K, V> head;
    Node<K, V> tail;

    public HashMap() {
        this(16, 0.75f);
    }

    public HashMap(int initialCapacity) {
        this(initialCapacity, 0.75f);
    }

    public HashMap(int initialCapacity, float loadFactor) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal initial capacity: " + initialCapacity);
        if (loadFactor <= 0 || loadFactor != loadFactor) throw new IllegalArgumentException("Illegal load factor: " + loadFactor);
        this.loadFactor = loadFactor;
        int cap = 1;
        while (cap < initialCapacity && cap < (1 << 30)) cap <<= 1;
        this.threshold = cap;  // table allocated lazily with this capacity
    }

    public HashMap(Map<? extends K, ? extends V> m) {
        this(Math.max((int) (m.size() / 0.75f) + 1, 16), 0.75f);
        putAll(m);
    }

    static int hash(Object key) {
        int h;
        return key == null ? 0 : (h = key.hashCode()) ^ (h >>> 16);
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    final Node<K, V> getNode(Object key) {
        Node<K, V>[] tab = table;
        if (tab == null) return null;
        int h = hash(key);
        for (Node<K, V> e = tab[h & (tab.length - 1)]; e != null; e = e.next) {
            if (e.hash == h && (e.key == key || (key != null && key.equals(e.key)))) return e;
        }
        return null;
    }

    public V get(Object key) {
        Node<K, V> e = getNode(key);
        if (e == null) return null;
        afterAccess(e);
        return e.value;
    }

    public boolean containsKey(Object key) {
        return getNode(key) != null;
    }

    public boolean containsValue(Object value) {
        for (Node<K, V> e = head; e != null; e = e.after) {
            if (Objects.equals(value, e.value)) return true;
        }
        return false;
    }

    @SuppressWarnings("unchecked")
    private void resize() {
        Node<K, V>[] old = table;
        int newCap = old == null ? Math.max(threshold, 1) : old.length << 1;
        if (old != null && old.length >= (1 << 30)) {
            threshold = Integer.MAX_VALUE;
            return;
        }
        Node<K, V>[] tab = (Node<K, V>[]) new Node[newCap];
        for (Node<K, V> e = head; e != null; e = e.after) {
            int i = e.hash & (newCap - 1);
            e.next = tab[i];
            tab[i] = e;
        }
        table = tab;
        threshold = (int) (newCap * loadFactor);
    }

    public V put(K key, V value) {
        if (table == null) resize();
        int h = hash(key);
        int i = h & (table.length - 1);
        for (Node<K, V> e = table[i]; e != null; e = e.next) {
            if (e.hash == h && (e.key == key || (key != null && key.equals(e.key)))) {
                V old = e.value;
                e.value = value;
                afterAccess(e);
                return old;
            }
        }
        Node<K, V> n = new Node<K, V>(h, key, value, table[i]);
        table[i] = n;
        linkLast(n);
        modCount++;
        if (++size > threshold) resize();
        afterInsertion();
        return null;
    }

    public V remove(Object key) {
        Node<K, V> e = removeNode(key);
        return e == null ? null : e.value;
    }

    final Node<K, V> removeNode(Object key) {
        Node<K, V>[] tab = table;
        if (tab == null) return null;
        int h = hash(key);
        int i = h & (tab.length - 1);
        Node<K, V> prev = null;
        for (Node<K, V> e = tab[i]; e != null; prev = e, e = e.next) {
            if (e.hash == h && (e.key == key || (key != null && key.equals(e.key)))) {
                if (prev == null) {
                    tab[i] = e.next;
                } else {
                    prev.next = e.next;
                }
                unlink(e);
                modCount++;
                size--;
                return e;
            }
        }
        return null;
    }

    public void clear() {
        if (table != null && size > 0) {
            for (int i = 0; i < table.length; i++) table[i] = null;
        }
        head = tail = null;
        size = 0;
        modCount++;
    }

    private void linkLast(Node<K, V> n) {
        n.before = tail;
        n.after = null;
        if (tail == null) {
            head = n;
        } else {
            tail.after = n;
        }
        tail = n;
    }

    private void unlink(Node<K, V> e) {
        if (e.before == null) {
            head = e.after;
        } else {
            e.before.after = e.after;
        }
        if (e.after == null) {
            tail = e.before;
        } else {
            e.after.before = e.before;
        }
        e.before = e.after = null;
    }

    /** Moves `e` to the end of the iteration order (LinkedHashMap access order). */
    final void moveToLast(Node<K, V> e) {
        if (tail == e) return;
        unlink(e);
        linkLast(e);
        modCount++;
    }

    void afterAccess(Node<K, V> e) {}

    void afterInsertion() {}

    @SuppressWarnings("unchecked")
    public Object clone() {
        HashMap<K, V> result;
        try {
            result = (HashMap<K, V>) super.clone();
        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
        result.table = null;
        result.head = result.tail = null;
        result.size = 0;
        result.modCount = 0;
        result.threshold = table == null ? threshold : table.length;
        for (Node<K, V> e = head; e != null; e = e.after) result.put(e.key, e.value);
        return result;
    }

    abstract class HashIterator {
        Node<K, V> next = head;
        Node<K, V> current;
        int expectedModCount = modCount;

        public final boolean hasNext() {
            return next != null;
        }

        final Node<K, V> nextNode() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            Node<K, V> e = next;
            if (e == null) throw new NoSuchElementException();
            current = e;
            next = e.after;
            return e;
        }

        public final void remove() {
            Node<K, V> p = current;
            if (p == null) throw new IllegalStateException();
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            current = null;
            removeNode(p.key);
            expectedModCount = modCount;
        }
    }

    final class KeyIterator extends HashIterator implements Iterator<K> {
        public K next() {
            return nextNode().key;
        }
    }

    final class ValueIterator extends HashIterator implements Iterator<V> {
        public V next() {
            return nextNode().value;
        }
    }

    final class EntryIterator extends HashIterator implements Iterator<Map.Entry<K, V>> {
        public Map.Entry<K, V> next() {
            return nextNode();
        }
    }

    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public int size() {
                return size;
            }

            public void clear() {
                HashMap.this.clear();
            }

            public Iterator<K> iterator() {
                return new KeyIterator();
            }

            public boolean contains(Object o) {
                return containsKey(o);
            }

            public boolean remove(Object key) {
                return removeNode(key) != null;
            }
        };
    }

    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public int size() {
                return size;
            }

            public void clear() {
                HashMap.this.clear();
            }

            public Iterator<V> iterator() {
                return new ValueIterator();
            }
        };
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() {
                return size;
            }

            public void clear() {
                HashMap.this.clear();
            }

            public Iterator<Map.Entry<K, V>> iterator() {
                return new EntryIterator();
            }

            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                Node<K, V> candidate = getNode(e.getKey());
                return candidate != null && candidate.equals(e);
            }

            public boolean remove(Object o) {
                if (!contains(o)) return false;
                removeNode(((Map.Entry<?, ?>) o).getKey());
                return true;
            }
        };
    }
}
