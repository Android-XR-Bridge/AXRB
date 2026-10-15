package java.util;

public abstract class AbstractMap<K, V> implements Map<K, V> {
    protected AbstractMap() {}

    public abstract Set<Map.Entry<K, V>> entrySet();

    public int size() {
        return entrySet().size();
    }

    public boolean isEmpty() {
        return size() == 0;
    }

    public boolean containsValue(Object value) {
        for (Map.Entry<K, V> e : entrySet()) {
            if (Objects.equals(value, e.getValue())) return true;
        }
        return false;
    }

    public boolean containsKey(Object key) {
        for (Map.Entry<K, V> e : entrySet()) {
            if (Objects.equals(key, e.getKey())) return true;
        }
        return false;
    }

    public V get(Object key) {
        for (Map.Entry<K, V> e : entrySet()) {
            if (Objects.equals(key, e.getKey())) return e.getValue();
        }
        return null;
    }

    public V put(K key, V value) {
        throw new UnsupportedOperationException();
    }

    public V remove(Object key) {
        Iterator<Map.Entry<K, V>> i = entrySet().iterator();
        while (i.hasNext()) {
            Map.Entry<K, V> e = i.next();
            if (Objects.equals(key, e.getKey())) {
                V old = e.getValue();
                i.remove();
                return old;
            }
        }
        return null;
    }

    public void putAll(Map<? extends K, ? extends V> m) {
        for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) put(e.getKey(), e.getValue());
    }

    public void clear() {
        entrySet().clear();
    }

    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public Iterator<K> iterator() {
                final Iterator<Map.Entry<K, V>> i = entrySet().iterator();
                return new Iterator<K>() {
                    public boolean hasNext() {
                        return i.hasNext();
                    }

                    public K next() {
                        return i.next().getKey();
                    }

                    public void remove() {
                        i.remove();
                    }
                };
            }

            public int size() {
                return AbstractMap.this.size();
            }

            public boolean contains(Object k) {
                return AbstractMap.this.containsKey(k);
            }
        };
    }

    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public Iterator<V> iterator() {
                final Iterator<Map.Entry<K, V>> i = entrySet().iterator();
                return new Iterator<V>() {
                    public boolean hasNext() {
                        return i.hasNext();
                    }

                    public V next() {
                        return i.next().getValue();
                    }

                    public void remove() {
                        i.remove();
                    }
                };
            }

            public int size() {
                return AbstractMap.this.size();
            }
        };
    }

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Map)) return false;
        Map<?, ?> m = (Map<?, ?>) o;
        if (m.size() != size()) return false;
        for (Map.Entry<K, V> e : entrySet()) {
            K key = e.getKey();
            V value = e.getValue();
            if (value == null) {
                if (!(m.get(key) == null && m.containsKey(key))) return false;
            } else if (!value.equals(m.get(key))) {
                return false;
            }
        }
        return true;
    }

    public int hashCode() {
        int h = 0;
        for (Map.Entry<K, V> e : entrySet()) h += e.hashCode();
        return h;
    }

    public String toString() {
        Iterator<Map.Entry<K, V>> i = entrySet().iterator();
        if (!i.hasNext()) return "{}";
        StringBuilder sb = new StringBuilder();
        sb.append('{');
        for (;;) {
            Map.Entry<K, V> e = i.next();
            K key = e.getKey();
            V value = e.getValue();
            sb.append(key == this ? "(this Map)" : String.valueOf(key));
            sb.append('=');
            sb.append(value == this ? "(this Map)" : String.valueOf(value));
            if (!i.hasNext()) return sb.append('}').toString();
            sb.append(',').append(' ');
        }
    }

    public static class SimpleEntry<K, V> implements Map.Entry<K, V>, java.io.Serializable {
        private final K key;
        private V value;

        public SimpleEntry(K key, V value) {
            this.key = key;
            this.value = value;
        }

        public SimpleEntry(Map.Entry<? extends K, ? extends V> entry) {
            this(entry.getKey(), entry.getValue());
        }

        public K getKey() {
            return key;
        }

        public V getValue() {
            return value;
        }

        public V setValue(V value) {
            V old = this.value;
            this.value = value;
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

    public static class SimpleImmutableEntry<K, V> extends SimpleEntry<K, V> {
        public SimpleImmutableEntry(K key, V value) {
            super(key, value);
        }

        public SimpleImmutableEntry(Map.Entry<? extends K, ? extends V> entry) {
            super(entry);
        }

        public V setValue(V value) {
            throw new UnsupportedOperationException();
        }
    }
}
