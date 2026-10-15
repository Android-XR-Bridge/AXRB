package java.util;

public class HashSet<E> extends AbstractSet<E> implements Set<E>, Cloneable, java.io.Serializable {
    private static final Object PRESENT = new Object();
    private HashMap<E, Object> map;

    public HashSet() {
        map = new HashMap<E, Object>();
    }

    public HashSet(Collection<? extends E> c) {
        map = new HashMap<E, Object>(Math.max((int) (c.size() / .75f) + 1, 16));
        addAll(c);
    }

    public HashSet(int initialCapacity) {
        map = new HashMap<E, Object>(initialCapacity);
    }

    public HashSet(int initialCapacity, float loadFactor) {
        map = new HashMap<E, Object>(initialCapacity, loadFactor);
    }

    HashSet(int initialCapacity, float loadFactor, boolean linked) {
        map = new LinkedHashMap<E, Object>(initialCapacity, loadFactor);
    }

    public Iterator<E> iterator() {
        return map.keySet().iterator();
    }

    public int size() {
        return map.size();
    }

    public boolean isEmpty() {
        return map.isEmpty();
    }

    public boolean contains(Object o) {
        return map.containsKey(o);
    }

    public boolean add(E e) {
        return map.put(e, PRESENT) == null;
    }

    public boolean remove(Object o) {
        return map.remove(o) == PRESENT;
    }

    public void clear() {
        map.clear();
    }

    @SuppressWarnings("unchecked")
    public Object clone() {
        try {
            HashSet<E> s = (HashSet<E>) super.clone();
            s.map = (HashMap<E, Object>) map.clone();
            return s;
        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
    }
}
