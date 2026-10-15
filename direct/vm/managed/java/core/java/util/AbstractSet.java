package java.util;

public abstract class AbstractSet<E> extends AbstractCollection<E> implements Set<E> {
    protected AbstractSet() {}

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Set)) return false;
        Collection<?> c = (Collection<?>) o;
        if (c.size() != size()) return false;
        try {
            return containsAll(c);
        } catch (ClassCastException | NullPointerException unused) {
            return false;
        }
    }

    public int hashCode() {
        int h = 0;
        for (E e : this) {
            if (e != null) h += e.hashCode();
        }
        return h;
    }
}
