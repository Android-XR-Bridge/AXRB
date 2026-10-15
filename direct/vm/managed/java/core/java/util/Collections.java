package java.util;

public final class Collections {
    private Collections() {}

    @SuppressWarnings("rawtypes")
    static final Comparator NATURAL_ORDER = new Comparator<Comparable<Object>>() {
        public int compare(Comparable<Object> a, Comparable<Object> b) {
            return a.compareTo(b);
        }
    };

    @SuppressWarnings("rawtypes")
    public static final List EMPTY_LIST = new UnmodifiableList<Object>(new ArrayList<Object>(0));
    @SuppressWarnings("rawtypes")
    public static final Set EMPTY_SET = new UnmodifiableSet<Object>(new HashSet<Object>(0));
    @SuppressWarnings("rawtypes")
    public static final Map EMPTY_MAP = new UnmodifiableMap<Object, Object>(new HashMap<Object, Object>(0));

    @SuppressWarnings("unchecked")
    public static <T> List<T> emptyList() {
        return (List<T>) EMPTY_LIST;
    }

    @SuppressWarnings("unchecked")
    public static <T> Set<T> emptySet() {
        return (Set<T>) EMPTY_SET;
    }

    @SuppressWarnings("unchecked")
    public static <K, V> Map<K, V> emptyMap() {
        return (Map<K, V>) EMPTY_MAP;
    }

    public static <T> Iterator<T> emptyIterator() {
        return Collections.<T>emptyList().iterator();
    }

    public static <T> List<T> singletonList(T o) {
        ArrayList<T> l = new ArrayList<T>(1);
        l.add(o);
        return new UnmodifiableList<T>(l);
    }

    public static <T> Set<T> singleton(T o) {
        HashSet<T> s = new HashSet<T>(1);
        s.add(o);
        return new UnmodifiableSet<T>(s);
    }

    public static <K, V> Map<K, V> singletonMap(K key, V value) {
        HashMap<K, V> m = new HashMap<K, V>(1);
        m.put(key, value);
        return new UnmodifiableMap<K, V>(m);
    }

    public static <T> List<T> nCopies(int n, T o) {
        if (n < 0) throw new IllegalArgumentException("List length = " + n);
        ArrayList<T> l = new ArrayList<T>(n);
        for (int i = 0; i < n; i++) l.add(o);
        return new UnmodifiableList<T>(l);
    }

    @SuppressWarnings("unchecked")
    public static <T extends Comparable<? super T>> void sort(List<T> list) {
        list.sort(null);
    }

    public static <T> void sort(List<T> list, Comparator<? super T> c) {
        list.sort(c);
    }

    public static void reverse(List<?> list) {
        @SuppressWarnings("unchecked")
        List<Object> l = (List<Object>) list;
        for (int i = 0, j = l.size() - 1; i < j; i++, j--) {
            Object t = l.get(i);
            l.set(i, l.get(j));
            l.set(j, t);
        }
    }

    public static void swap(List<?> list, int i, int j) {
        @SuppressWarnings("unchecked")
        List<Object> l = (List<Object>) list;
        l.set(i, l.set(j, l.get(i)));
    }

    public static void shuffle(List<?> list) {
        shuffle(list, new Random());
    }

    public static void shuffle(List<?> list, Random rnd) {
        for (int i = list.size(); i > 1; i--) swap(list, i - 1, rnd.nextInt(i));
    }

    @SuppressWarnings("unchecked")
    public static <T> Comparator<T> reverseOrder() {
        return (Comparator<T>) NATURAL_ORDER.reversed();
    }

    public static <T> Comparator<T> reverseOrder(Comparator<T> cmp) {
        if (cmp == null) return reverseOrder();
        return cmp.reversed();
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public static <T extends Object & Comparable<? super T>> T max(Collection<? extends T> coll) {
        return Collections.<T>max(coll, (Comparator<? super T>) NATURAL_ORDER);
    }

    public static <T> T max(Collection<? extends T> coll, Comparator<? super T> comp) {
        Iterator<? extends T> i = coll.iterator();
        T best = i.next();
        while (i.hasNext()) {
            T next = i.next();
            if (comp.compare(next, best) > 0) best = next;
        }
        return best;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public static <T extends Object & Comparable<? super T>> T min(Collection<? extends T> coll) {
        return Collections.<T>min(coll, (Comparator<? super T>) NATURAL_ORDER);
    }

    public static <T> T min(Collection<? extends T> coll, Comparator<? super T> comp) {
        Iterator<? extends T> i = coll.iterator();
        T best = i.next();
        while (i.hasNext()) {
            T next = i.next();
            if (comp.compare(next, best) < 0) best = next;
        }
        return best;
    }

    @SafeVarargs
    public static <T> boolean addAll(Collection<? super T> c, T... elements) {
        boolean result = false;
        for (T e : elements) result |= c.add(e);
        return result;
    }

    public static int frequency(Collection<?> c, Object o) {
        int n = 0;
        for (Object e : c) {
            if (Objects.equals(o, e)) n++;
        }
        return n;
    }

    public static <T> List<T> unmodifiableList(List<? extends T> list) {
        return new UnmodifiableList<T>(list);
    }

    public static <T> Set<T> unmodifiableSet(Set<? extends T> s) {
        return new UnmodifiableSet<T>(s);
    }

    public static <T> Collection<T> unmodifiableCollection(Collection<? extends T> c) {
        return new UnmodifiableCollection<T>(c);
    }

    public static <K, V> Map<K, V> unmodifiableMap(Map<? extends K, ? extends V> m) {
        return new UnmodifiableMap<K, V>(m);
    }

    public static <T> List<T> synchronizedList(List<T> list) {
        return new SynchronizedList<T>(list);
    }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> m) {
        return new SynchronizedMap<K, V>(m);
    }

    public static <T> Set<T> synchronizedSet(final Set<T> s) {
        Objects.requireNonNull(s);
        return new AbstractSet<T>() {
            public int size() {
                synchronized (this) {
                    return s.size();
                }
            }

            public boolean contains(Object o) {
                synchronized (this) {
                    return s.contains(o);
                }
            }

            public boolean add(T e) {
                synchronized (this) {
                    return s.add(e);
                }
            }

            public boolean remove(Object o) {
                synchronized (this) {
                    return s.remove(o);
                }
            }

            public void clear() {
                synchronized (this) {
                    s.clear();
                }
            }

            // Like the JDK's, iteration must be synchronized by the caller.
            public Iterator<T> iterator() {
                return s.iterator();
            }
        };
    }

    public static <E> Set<E> newSetFromMap(final Map<E, Boolean> map) {
        if (!map.isEmpty()) throw new IllegalArgumentException("Map is non-empty");
        return new AbstractSet<E>() {
            public int size() {
                return map.size();
            }

            public boolean contains(Object o) {
                return map.containsKey(o);
            }

            public boolean add(E e) {
                return map.put(e, Boolean.TRUE) == null;
            }

            public boolean remove(Object o) {
                return map.remove(o) != null;
            }

            public void clear() {
                map.clear();
            }

            public Iterator<E> iterator() {
                return map.keySet().iterator();
            }
        };
    }

    static class UnmodifiableCollection<E> implements Collection<E>, java.io.Serializable {
        final Collection<? extends E> c;

        UnmodifiableCollection(Collection<? extends E> c) {
            this.c = Objects.requireNonNull(c);
        }

        public int size() {
            return c.size();
        }

        public boolean isEmpty() {
            return c.isEmpty();
        }

        public boolean contains(Object o) {
            return c.contains(o);
        }

        public Object[] toArray() {
            return c.toArray();
        }

        public <T> T[] toArray(T[] a) {
            return c.toArray(a);
        }

        public String toString() {
            return c.toString();
        }

        public Iterator<E> iterator() {
            final Iterator<? extends E> i = c.iterator();
            return new Iterator<E>() {
                public boolean hasNext() {
                    return i.hasNext();
                }

                public E next() {
                    return i.next();
                }
            };
        }

        public boolean add(E e) {
            throw new UnsupportedOperationException();
        }

        public boolean remove(Object o) {
            throw new UnsupportedOperationException();
        }

        public boolean containsAll(Collection<?> coll) {
            return c.containsAll(coll);
        }

        public boolean addAll(Collection<? extends E> coll) {
            throw new UnsupportedOperationException();
        }

        public boolean removeAll(Collection<?> coll) {
            throw new UnsupportedOperationException();
        }

        public boolean retainAll(Collection<?> coll) {
            throw new UnsupportedOperationException();
        }

        public void clear() {
            throw new UnsupportedOperationException();
        }
    }

    static class UnmodifiableSet<E> extends UnmodifiableCollection<E> implements Set<E> {
        UnmodifiableSet(Set<? extends E> s) {
            super(s);
        }

        public boolean equals(Object o) {
            return o == this || c.equals(o);
        }

        public int hashCode() {
            return c.hashCode();
        }
    }

    static class UnmodifiableList<E> extends UnmodifiableCollection<E> implements List<E>, RandomAccess {
        final List<? extends E> list;

        UnmodifiableList(List<? extends E> list) {
            super(list);
            this.list = list;
        }

        public boolean equals(Object o) {
            return o == this || list.equals(o);
        }

        public int hashCode() {
            return list.hashCode();
        }

        public E get(int index) {
            return list.get(index);
        }

        public E set(int index, E element) {
            throw new UnsupportedOperationException();
        }

        public void add(int index, E element) {
            throw new UnsupportedOperationException();
        }

        public E remove(int index) {
            throw new UnsupportedOperationException();
        }

        public int indexOf(Object o) {
            return list.indexOf(o);
        }

        public int lastIndexOf(Object o) {
            return list.lastIndexOf(o);
        }

        public boolean addAll(int index, Collection<? extends E> c) {
            throw new UnsupportedOperationException();
        }

        public void sort(Comparator<? super E> c) {
            throw new UnsupportedOperationException();
        }

        public ListIterator<E> listIterator() {
            return listIterator(0);
        }

        public ListIterator<E> listIterator(final int index) {
            final ListIterator<? extends E> i = list.listIterator(index);
            return new ListIterator<E>() {
                public boolean hasNext() {
                    return i.hasNext();
                }

                public E next() {
                    return i.next();
                }

                public boolean hasPrevious() {
                    return i.hasPrevious();
                }

                public E previous() {
                    return i.previous();
                }

                public int nextIndex() {
                    return i.nextIndex();
                }

                public int previousIndex() {
                    return i.previousIndex();
                }

                public void remove() {
                    throw new UnsupportedOperationException();
                }

                public void set(E e) {
                    throw new UnsupportedOperationException();
                }

                public void add(E e) {
                    throw new UnsupportedOperationException();
                }
            };
        }

        public List<E> subList(int fromIndex, int toIndex) {
            return new UnmodifiableList<E>(list.subList(fromIndex, toIndex));
        }
    }

    static class UnmodifiableMap<K, V> implements Map<K, V>, java.io.Serializable {
        private final Map<? extends K, ? extends V> m;

        UnmodifiableMap(Map<? extends K, ? extends V> m) {
            this.m = Objects.requireNonNull(m);
        }

        public int size() {
            return m.size();
        }

        public boolean isEmpty() {
            return m.isEmpty();
        }

        public boolean containsKey(Object key) {
            return m.containsKey(key);
        }

        public boolean containsValue(Object val) {
            return m.containsValue(val);
        }

        public V get(Object key) {
            return m.get(key);
        }

        public V put(K key, V value) {
            throw new UnsupportedOperationException();
        }

        public V remove(Object key) {
            throw new UnsupportedOperationException();
        }

        public void putAll(Map<? extends K, ? extends V> map) {
            throw new UnsupportedOperationException();
        }

        public void clear() {
            throw new UnsupportedOperationException();
        }

        @SuppressWarnings("unchecked")
        public Set<K> keySet() {
            return new UnmodifiableSet<K>((Set<K>) m.keySet());
        }

        @SuppressWarnings({"unchecked", "rawtypes"})
        public Set<Map.Entry<K, V>> entrySet() {
            // Entries are copied into immutable ones so setValue cannot write through.
            ArrayList<Map.Entry<K, V>> copy = new ArrayList<Map.Entry<K, V>>(m.size());
            for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) {
                copy.add(new AbstractMap.SimpleImmutableEntry<K, V>(e.getKey(), e.getValue()));
            }
            LinkedHashSet<Map.Entry<K, V>> s = new LinkedHashSet<Map.Entry<K, V>>(copy);
            return new UnmodifiableSet<Map.Entry<K, V>>(s);
        }

        @SuppressWarnings("unchecked")
        public Collection<V> values() {
            return new UnmodifiableCollection<V>((Collection<V>) m.values());
        }

        public boolean equals(Object o) {
            return o == this || m.equals(o);
        }

        public int hashCode() {
            return m.hashCode();
        }

        public String toString() {
            return m.toString();
        }
    }

    static class SynchronizedList<E> extends AbstractList<E> implements RandomAccess {
        private final List<E> list;

        SynchronizedList(List<E> list) {
            this.list = Objects.requireNonNull(list);
        }

        public synchronized int size() {
            return list.size();
        }

        public synchronized E get(int index) {
            return list.get(index);
        }

        public synchronized E set(int index, E element) {
            return list.set(index, element);
        }

        public synchronized void add(int index, E element) {
            list.add(index, element);
            modCount++;
        }

        public synchronized E remove(int index) {
            modCount++;
            return list.remove(index);
        }
    }

    static class SynchronizedMap<K, V> extends AbstractMap<K, V> {
        private final Map<K, V> m;

        SynchronizedMap(Map<K, V> m) {
            this.m = Objects.requireNonNull(m);
        }

        public synchronized int size() {
            return m.size();
        }

        public synchronized boolean containsKey(Object key) {
            return m.containsKey(key);
        }

        public synchronized V get(Object key) {
            return m.get(key);
        }

        public synchronized V put(K key, V value) {
            return m.put(key, value);
        }

        public synchronized V remove(Object key) {
            return m.remove(key);
        }

        public synchronized void clear() {
            m.clear();
        }

        /** As in OpenJDK, iteration must be synchronized on the map by the caller. */
        public Set<Map.Entry<K, V>> entrySet() {
            return m.entrySet();
        }
    }
}
