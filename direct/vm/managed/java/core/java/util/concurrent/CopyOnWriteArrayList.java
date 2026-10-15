package java.util.concurrent;

import java.util.AbstractList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.RandomAccess;

public class CopyOnWriteArrayList<E> extends AbstractList<E> implements RandomAccess, Cloneable, java.io.Serializable {
    private final Object lock = new Object();
    private volatile Object[] array;

    public CopyOnWriteArrayList() {
        array = new Object[0];
    }

    public CopyOnWriteArrayList(Collection<? extends E> c) {
        array = c.toArray();
    }

    public CopyOnWriteArrayList(E[] toCopyIn) {
        array = Arrays.copyOf(toCopyIn, toCopyIn.length, Object[].class);
    }

    public int size() {
        return array.length;
    }

    @SuppressWarnings("unchecked")
    public E get(int index) {
        return (E) array[index];
    }

    public E set(int index, E element) {
        synchronized (lock) {
            Object[] a = array.clone();
            E old = get(index);
            a[index] = element;
            array = a;
            return old;
        }
    }

    public boolean add(E e) {
        synchronized (lock) {
            Object[] a = Arrays.copyOf(array, array.length + 1);
            a[a.length - 1] = e;
            array = a;
            return true;
        }
    }

    public void add(int index, E element) {
        synchronized (lock) {
            Object[] old = array;
            if (index < 0 || index > old.length) throw new IndexOutOfBoundsException("Index: " + index);
            Object[] a = new Object[old.length + 1];
            System.arraycopy(old, 0, a, 0, index);
            a[index] = element;
            System.arraycopy(old, index, a, index + 1, old.length - index);
            array = a;
        }
    }

    public E remove(int index) {
        synchronized (lock) {
            Object[] old = array;
            E value = get(index);
            Object[] a = new Object[old.length - 1];
            System.arraycopy(old, 0, a, 0, index);
            System.arraycopy(old, index + 1, a, index, old.length - index - 1);
            array = a;
            return value;
        }
    }

    public boolean remove(Object o) {
        synchronized (lock) {
            int i = indexOf(o);
            if (i < 0) return false;
            remove(i);
            return true;
        }
    }

    public boolean addIfAbsent(E e) {
        synchronized (lock) {
            if (indexOf(e) >= 0) return false;
            return add(e);
        }
    }

    public int addAllAbsent(Collection<? extends E> c) {
        int added = 0;
        synchronized (lock) {
            for (E e : c) if (addIfAbsent(e)) added++;
        }
        return added;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] more = c.toArray();
        if (more.length == 0) return false;
        synchronized (lock) {
            Object[] a = Arrays.copyOf(array, array.length + more.length);
            System.arraycopy(more, 0, a, array.length, more.length);
            array = a;
            return true;
        }
    }

    public void clear() {
        synchronized (lock) {
            array = new Object[0];
        }
    }

    public int indexOf(Object o) {
        Object[] a = array;
        for (int i = 0; i < a.length; i++) {
            if (o == null ? a[i] == null : o.equals(a[i])) return i;
        }
        return -1;
    }

    public boolean contains(Object o) {
        return indexOf(o) >= 0;
    }

    public Object[] toArray() {
        return array.clone();
    }

    public Object clone() {
        return new CopyOnWriteArrayList<E>(this);
    }

    /** Iterates a snapshot; never throws ConcurrentModificationException. */
    public Iterator<E> iterator() {
        final Object[] snapshot = array;
        return new Iterator<E>() {
            int next;

            public boolean hasNext() {
                return next < snapshot.length;
            }

            @SuppressWarnings("unchecked")
            public E next() {
                if (next >= snapshot.length) throw new NoSuchElementException();
                return (E) snapshot[next++];
            }

            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
    }
}
