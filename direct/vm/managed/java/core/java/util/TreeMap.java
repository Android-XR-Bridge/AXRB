package java.util;

/**
 * A sorted array with binary search instead of a red-black tree. Range views
 * (headMap, subMap, descendingMap, ...) are snapshots, not live views.
 */
public class TreeMap<K, V> extends AbstractMap<K, V> implements NavigableMap<K, V>, Cloneable, java.io.Serializable {
    static final class Node<K, V> implements Map.Entry<K, V> {
        final K key;
        V value;

        Node(K key, V value) {
            this.key = key;
            this.value = value;
        }

        public K getKey() {
            return key;
        }

        public V getValue() {
            return value;
        }

        public V setValue(V v) {
            V old = value;
            value = v;
            return old;
        }

        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue());
        }

        public int hashCode() {
            return Objects.hashCode(key) ^ Objects.hashCode(value);
        }

        public String toString() {
            return key + "=" + value;
        }
    }

    private final Comparator<? super K> comparator;
    private ArrayList<Node<K, V>> nodes = new ArrayList<Node<K, V>>();
    private int modCount;

    public TreeMap() {
        comparator = null;
    }

    public TreeMap(Comparator<? super K> comparator) {
        this.comparator = comparator;
    }

    public TreeMap(Map<? extends K, ? extends V> m) {
        comparator = m instanceof SortedMap ? ((SortedMap<K, ? extends V>) m).comparator() : null;
        putAll(m);
    }

    @SuppressWarnings("unchecked")
    private int compare(Object a, Object b) {
        return comparator != null ? comparator.compare((K) a, (K) b) : ((Comparable<Object>) a).compareTo(b);
    }

    /** Index of key, or -(insertion point) - 1. */
    private int search(Object key) {
        if (key == null && comparator == null) throw new NullPointerException();
        int lo = 0, hi = nodes.size() - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = compare(nodes.get(mid).key, key);
            if (c < 0) lo = mid + 1;
            else if (c > 0) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }

    public Comparator<? super K> comparator() {
        return comparator;
    }

    public int size() {
        return nodes.size();
    }

    public boolean containsKey(Object key) {
        return search(key) >= 0;
    }

    public V get(Object key) {
        int i = search(key);
        return i >= 0 ? nodes.get(i).value : null;
    }

    public V put(K key, V value) {
        if (nodes.isEmpty()) compare(key, key);
        int i = search(key);
        if (i >= 0) return nodes.get(i).setValue(value);
        nodes.add(-(i + 1), new Node<K, V>(key, value));
        modCount++;
        return null;
    }

    public V remove(Object key) {
        int i = search(key);
        if (i < 0) return null;
        modCount++;
        return nodes.remove(i).value;
    }

    public void clear() {
        nodes.clear();
        modCount++;
    }

    public Object clone() {
        TreeMap<K, V> m = new TreeMap<K, V>(comparator);
        for (Node<K, V> n : nodes) m.nodes.add(new Node<K, V>(n.key, n.value));
        return m;
    }

    private Node<K, V> at(int i) {
        return i >= 0 && i < nodes.size() ? nodes.get(i) : null;
    }

    private static <K> K key(Map.Entry<K, ?> e) {
        return e == null ? null : e.getKey();
    }

    private int floorIndex(K key) {
        int i = search(key);
        return i >= 0 ? i : -(i + 1) - 1;
    }

    private int ceilingIndex(K key) {
        int i = search(key);
        return i >= 0 ? i : -(i + 1);
    }

    public Map.Entry<K, V> lowerEntry(K key) {
        return at(ceilingIndex(key) - 1);
    }

    public K lowerKey(K key) {
        return key(lowerEntry(key));
    }

    public Map.Entry<K, V> floorEntry(K key) {
        return at(floorIndex(key));
    }

    public K floorKey(K key) {
        return key(floorEntry(key));
    }

    public Map.Entry<K, V> ceilingEntry(K key) {
        return at(ceilingIndex(key));
    }

    public K ceilingKey(K key) {
        return key(ceilingEntry(key));
    }

    public Map.Entry<K, V> higherEntry(K key) {
        return at(floorIndex(key) + 1);
    }

    public K higherKey(K key) {
        return key(higherEntry(key));
    }

    public Map.Entry<K, V> firstEntry() {
        return at(0);
    }

    public Map.Entry<K, V> lastEntry() {
        return at(nodes.size() - 1);
    }

    public K firstKey() {
        if (nodes.isEmpty()) throw new NoSuchElementException();
        return nodes.get(0).key;
    }

    public K lastKey() {
        if (nodes.isEmpty()) throw new NoSuchElementException();
        return nodes.get(nodes.size() - 1).key;
    }

    public Map.Entry<K, V> pollFirstEntry() {
        if (nodes.isEmpty()) return null;
        modCount++;
        return nodes.remove(0);
    }

    public Map.Entry<K, V> pollLastEntry() {
        if (nodes.isEmpty()) return null;
        modCount++;
        return nodes.remove(nodes.size() - 1);
    }

    private TreeMap<K, V> range(int from, int to) {
        TreeMap<K, V> m = new TreeMap<K, V>(comparator);
        for (int i = Math.max(from, 0); i < Math.min(to, nodes.size()); i++) {
            Node<K, V> n = nodes.get(i);
            m.nodes.add(new Node<K, V>(n.key, n.value));
        }
        return m;
    }

    public NavigableMap<K, V> subMap(K fromKey, boolean fromInclusive, K toKey, boolean toInclusive) {
        int from = fromInclusive ? ceilingIndex(fromKey) : floorIndex(fromKey) + 1;
        int to = toInclusive ? floorIndex(toKey) + 1 : ceilingIndex(toKey);
        return range(from, to);
    }

    public NavigableMap<K, V> headMap(K toKey, boolean inclusive) {
        return range(0, inclusive ? floorIndex(toKey) + 1 : ceilingIndex(toKey));
    }

    public NavigableMap<K, V> tailMap(K fromKey, boolean inclusive) {
        return range(inclusive ? ceilingIndex(fromKey) : floorIndex(fromKey) + 1, nodes.size());
    }

    public SortedMap<K, V> subMap(K fromKey, K toKey) {
        return subMap(fromKey, true, toKey, false);
    }

    public SortedMap<K, V> headMap(K toKey) {
        return headMap(toKey, false);
    }

    public SortedMap<K, V> tailMap(K fromKey) {
        return tailMap(fromKey, true);
    }

    public NavigableMap<K, V> descendingMap() {
        TreeMap<K, V> m = new TreeMap<K, V>(Collections.reverseOrder(new Comparator<K>() {
            public int compare(K a, K b) {
                return TreeMap.this.compare(a, b);
            }
        }));
        for (int i = nodes.size() - 1; i >= 0; i--) {
            Node<K, V> n = nodes.get(i);
            m.nodes.add(new Node<K, V>(n.key, n.value));
        }
        return m;
    }

    public NavigableSet<K> navigableKeySet() {
        TreeSet<K> s = new TreeSet<K>(comparator);
        for (Node<K, V> n : nodes) s.add(n.key);
        return s;
    }

    public NavigableSet<K> descendingKeySet() {
        return descendingMap().navigableKeySet();
    }

    private abstract class Itr<T> implements Iterator<T> {
        int next, last = -1, expected = modCount;

        public boolean hasNext() {
            return next < nodes.size();
        }

        Node<K, V> nextNode() {
            if (modCount != expected) throw new ConcurrentModificationException();
            if (next >= nodes.size()) throw new NoSuchElementException();
            last = next;
            return nodes.get(next++);
        }

        public void remove() {
            if (last < 0) throw new IllegalStateException();
            if (modCount != expected) throw new ConcurrentModificationException();
            nodes.remove(last);
            next = last;
            last = -1;
            expected = ++modCount;
        }
    }

    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public int size() {
                return nodes.size();
            }

            public boolean contains(Object o) {
                return containsKey(o);
            }

            public boolean remove(Object o) {
                int before = nodes.size();
                TreeMap.this.remove(o);
                return nodes.size() != before;
            }

            public void clear() {
                TreeMap.this.clear();
            }

            public Iterator<K> iterator() {
                return new Itr<K>() {
                    public K next() {
                        return nextNode().key;
                    }
                };
            }
        };
    }

    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public int size() {
                return nodes.size();
            }

            public void clear() {
                TreeMap.this.clear();
            }

            public Iterator<V> iterator() {
                return new Itr<V>() {
                    public V next() {
                        return nextNode().value;
                    }
                };
            }
        };
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() {
                return nodes.size();
            }

            public void clear() {
                TreeMap.this.clear();
            }

            public Iterator<Map.Entry<K, V>> iterator() {
                return new Itr<Map.Entry<K, V>>() {
                    public Map.Entry<K, V> next() {
                        return nextNode();
                    }
                };
            }
        };
    }
}
