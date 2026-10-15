package java.util;

import java.util.function.Consumer;
import java.util.function.Predicate;

public class ArrayList<E> extends AbstractList<E> implements List<E>, RandomAccess, Cloneable, java.io.Serializable {
    private static final Object[] EMPTY = {};

    private transient Object[] elementData;
    private int size;

    public ArrayList() {
        elementData = EMPTY;
    }

    public ArrayList(int initialCapacity) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal Capacity: " + initialCapacity);
        elementData = initialCapacity == 0 ? EMPTY : new Object[initialCapacity];
    }

    public ArrayList(Collection<? extends E> c) {
        Object[] a = c.toArray();
        elementData = a.length == 0 ? EMPTY : Arrays.copyOf(a, a.length, Object[].class);
        size = a.length;
    }

    public void ensureCapacity(int minCapacity) {
        if (minCapacity > elementData.length) grow(minCapacity);
    }

    public void trimToSize() {
        modCount++;
        if (size < elementData.length) elementData = size == 0 ? EMPTY : Arrays.copyOf(elementData, size);
    }

    private void grow(int minCapacity) {
        int old = elementData.length;
        int n = old + (old >> 1);
        if (n < minCapacity) n = minCapacity;
        if (n < 10) n = 10;
        if (n < 0) throw new OutOfMemoryError("Required array length too large");
        elementData = Arrays.copyOf(elementData, n);
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    public boolean contains(Object o) {
        return indexOf(o) >= 0;
    }

    public int indexOf(Object o) {
        for (int i = 0; i < size; i++) {
            if (Objects.equals(o, elementData[i])) return i;
        }
        return -1;
    }

    public int lastIndexOf(Object o) {
        for (int i = size - 1; i >= 0; i--) {
            if (Objects.equals(o, elementData[i])) return i;
        }
        return -1;
    }

    public Object clone() {
        try {
            @SuppressWarnings("unchecked")
            ArrayList<E> v = (ArrayList<E>) super.clone();
            v.elementData = Arrays.copyOf(elementData, size);
            v.modCount = 0;
            return v;
        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
    }

    public Object[] toArray() {
        return Arrays.copyOf(elementData, size);
    }

    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        if (a.length < size) return (T[]) Arrays.copyOf(elementData, size, a.getClass());
        System.arraycopy(elementData, 0, a, 0, size);
        if (a.length > size) a[size] = null;
        return a;
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= size) throw new IndexOutOfBoundsException("Index: " + index + ", Size: " + size);
    }

    @SuppressWarnings("unchecked")
    public E get(int index) {
        checkIndex(index);
        return (E) elementData[index];
    }

    @SuppressWarnings("unchecked")
    public E set(int index, E element) {
        checkIndex(index);
        E old = (E) elementData[index];
        elementData[index] = element;
        return old;
    }

    public boolean add(E e) {
        modCount++;
        if (size == elementData.length) grow(size + 1);
        elementData[size++] = e;
        return true;
    }

    public void add(int index, E element) {
        checkPositionIndex(index, size);
        modCount++;
        if (size == elementData.length) grow(size + 1);
        System.arraycopy(elementData, index, elementData, index + 1, size - index);
        elementData[index] = element;
        size++;
    }

    @SuppressWarnings("unchecked")
    public E remove(int index) {
        checkIndex(index);
        modCount++;
        E old = (E) elementData[index];
        fastRemove(index);
        return old;
    }

    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i < 0) return false;
        modCount++;
        fastRemove(i);
        return true;
    }

    private void fastRemove(int index) {
        int moved = size - index - 1;
        if (moved > 0) System.arraycopy(elementData, index + 1, elementData, index, moved);
        elementData[--size] = null;
    }

    public void clear() {
        modCount++;
        for (int i = 0; i < size; i++) elementData[i] = null;
        size = 0;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] a = c.toArray();
        modCount++;
        if (a.length == 0) return false;
        if (size + a.length > elementData.length) grow(size + a.length);
        System.arraycopy(a, 0, elementData, size, a.length);
        size += a.length;
        return true;
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        checkPositionIndex(index, size);
        Object[] a = c.toArray();
        modCount++;
        if (a.length == 0) return false;
        if (size + a.length > elementData.length) grow(size + a.length);
        System.arraycopy(elementData, index, elementData, index + a.length, size - index);
        System.arraycopy(a, 0, elementData, index, a.length);
        size += a.length;
        return true;
    }

    protected void removeRange(int fromIndex, int toIndex) {
        if (fromIndex > toIndex) throw new IndexOutOfBoundsException("From Index: " + fromIndex + " > To Index: " + toIndex);
        modCount++;
        System.arraycopy(elementData, toIndex, elementData, fromIndex, size - toIndex);
        int newSize = size - (toIndex - fromIndex);
        for (int i = newSize; i < size; i++) elementData[i] = null;
        size = newSize;
    }

    @SuppressWarnings("unchecked")
    public boolean removeIf(Predicate<? super E> filter) {
        Objects.requireNonNull(filter);
        int expected = modCount;
        int w = 0;
        for (int r = 0; r < size; r++) {
            E e = (E) elementData[r];
            if (!filter.test(e)) elementData[w++] = e;
            if (modCount != expected) throw new ConcurrentModificationException();
        }
        if (w == size) return false;
        for (int i = w; i < size; i++) elementData[i] = null;
        size = w;
        modCount++;
        return true;
    }

    @SuppressWarnings("unchecked")
    public void forEach(Consumer<? super E> action) {
        Objects.requireNonNull(action);
        int expected = modCount;
        for (int i = 0; i < size && modCount == expected; i++) action.accept((E) elementData[i]);
        if (modCount != expected) throw new ConcurrentModificationException();
    }

    @SuppressWarnings("unchecked")
    public void sort(Comparator<? super E> c) {
        int expected = modCount;
        Arrays.sort((E[]) elementData, 0, size, c);
        if (modCount != expected) throw new ConcurrentModificationException();
        modCount++;
    }
}
