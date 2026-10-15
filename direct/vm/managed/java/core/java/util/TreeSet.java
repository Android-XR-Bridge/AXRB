package java.util;

/** Backed by TreeMap; range views are snapshots like TreeMap's. */
public class TreeSet<E> extends AbstractSet<E> implements NavigableSet<E>, Cloneable, java.io.Serializable {
    private static final Object PRESENT = new Object();
    private TreeMap<E, Object> m;

    private TreeSet(TreeMap<E, Object> m) {
        this.m = m;
    }

    public TreeSet() {
        m = new TreeMap<E, Object>();
    }

    public TreeSet(Comparator<? super E> comparator) {
        m = new TreeMap<E, Object>(comparator);
    }

    public TreeSet(Collection<? extends E> c) {
        this(c instanceof SortedSet ? ((SortedSet<E>) c).comparator() : null);
        addAll(c);
    }

    public TreeSet(SortedSet<E> s) {
        this(s.comparator());
        addAll(s);
    }

    private static <E> TreeSet<E> keys(NavigableMap<E, Object> map) {
        return new TreeSet<E>((TreeMap<E, Object>) map);
    }

    public Iterator<E> iterator() {
        return m.keySet().iterator();
    }

    public Iterator<E> descendingIterator() {
        return descendingSet().iterator();
    }

    public NavigableSet<E> descendingSet() {
        return keys(m.descendingMap());
    }

    public int size() {
        return m.size();
    }

    public boolean contains(Object o) {
        return m.containsKey(o);
    }

    public boolean add(E e) {
        return m.put(e, PRESENT) == null;
    }

    public boolean remove(Object o) {
        return m.remove(o) == PRESENT;
    }

    public void clear() {
        m.clear();
    }

    @SuppressWarnings("unchecked")
    public Object clone() {
        return new TreeSet<E>((TreeMap<E, Object>) m.clone());
    }

    public Comparator<? super E> comparator() {
        return m.comparator();
    }

    public E first() {
        return m.firstKey();
    }

    public E last() {
        return m.lastKey();
    }

    public E lower(E e) {
        return m.lowerKey(e);
    }

    public E floor(E e) {
        return m.floorKey(e);
    }

    public E ceiling(E e) {
        return m.ceilingKey(e);
    }

    public E higher(E e) {
        return m.higherKey(e);
    }

    public E pollFirst() {
        Map.Entry<E, ?> e = m.pollFirstEntry();
        return e == null ? null : e.getKey();
    }

    public E pollLast() {
        Map.Entry<E, ?> e = m.pollLastEntry();
        return e == null ? null : e.getKey();
    }

    public NavigableSet<E> subSet(E fromElement, boolean fromInclusive, E toElement, boolean toInclusive) {
        return keys(m.subMap(fromElement, fromInclusive, toElement, toInclusive));
    }

    public NavigableSet<E> headSet(E toElement, boolean inclusive) {
        return keys(m.headMap(toElement, inclusive));
    }

    public NavigableSet<E> tailSet(E fromElement, boolean inclusive) {
        return keys(m.tailMap(fromElement, inclusive));
    }

    public SortedSet<E> subSet(E fromElement, E toElement) {
        return subSet(fromElement, true, toElement, false);
    }

    public SortedSet<E> headSet(E toElement) {
        return headSet(toElement, false);
    }

    public SortedSet<E> tailSet(E fromElement) {
        return tailSet(fromElement, true);
    }
}
