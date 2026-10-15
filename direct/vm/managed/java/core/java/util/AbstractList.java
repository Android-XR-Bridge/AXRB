package java.util;

public abstract class AbstractList<E> extends AbstractCollection<E> implements List<E> {
    protected transient int modCount;

    protected AbstractList() {}

    public boolean add(E e) {
        add(size(), e);
        return true;
    }

    public abstract E get(int index);

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
        for (int i = 0, n = size(); i < n; i++) {
            if (Objects.equals(o, get(i))) return i;
        }
        return -1;
    }

    public int lastIndexOf(Object o) {
        for (int i = size() - 1; i >= 0; i--) {
            if (Objects.equals(o, get(i))) return i;
        }
        return -1;
    }

    public void clear() {
        removeRange(0, size());
    }

    protected void removeRange(int fromIndex, int toIndex) {
        for (int i = toIndex - 1; i >= fromIndex; i--) remove(i);
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        checkPositionIndex(index, size());
        boolean modified = false;
        for (E e : c) {
            add(index++, e);
            modified = true;
        }
        return modified;
    }

    public Iterator<E> iterator() {
        return new Itr(0);
    }

    public ListIterator<E> listIterator() {
        return new Itr(0);
    }

    public ListIterator<E> listIterator(int index) {
        checkPositionIndex(index, size());
        return new Itr(index);
    }

    static void checkPositionIndex(int index, int size) {
        if (index < 0 || index > size) throw new IndexOutOfBoundsException("Index: " + index + ", Size: " + size);
    }

    private class Itr implements ListIterator<E> {
        int cursor;
        int lastRet = -1;
        int expectedModCount = modCount;

        Itr(int index) {
            cursor = index;
        }

        public boolean hasNext() {
            return cursor != size();
        }

        public E next() {
            checkForComodification();
            int i = cursor;
            if (i >= size()) throw new NoSuchElementException();
            E next = get(i);
            lastRet = i;
            cursor = i + 1;
            return next;
        }

        public boolean hasPrevious() {
            return cursor != 0;
        }

        public E previous() {
            checkForComodification();
            int i = cursor - 1;
            if (i < 0) throw new NoSuchElementException();
            E previous = get(i);
            lastRet = cursor = i;
            return previous;
        }

        public int nextIndex() {
            return cursor;
        }

        public int previousIndex() {
            return cursor - 1;
        }

        public void remove() {
            if (lastRet < 0) throw new IllegalStateException();
            checkForComodification();
            AbstractList.this.remove(lastRet);
            if (lastRet < cursor) cursor--;
            lastRet = -1;
            expectedModCount = modCount;
        }

        public void set(E e) {
            if (lastRet < 0) throw new IllegalStateException();
            checkForComodification();
            AbstractList.this.set(lastRet, e);
            expectedModCount = modCount;
        }

        public void add(E e) {
            checkForComodification();
            AbstractList.this.add(cursor++, e);
            lastRet = -1;
            expectedModCount = modCount;
        }

        final void checkForComodification() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
        }
    }

    public List<E> subList(int fromIndex, int toIndex) {
        if (fromIndex < 0) throw new IndexOutOfBoundsException("fromIndex = " + fromIndex);
        if (toIndex > size()) throw new IndexOutOfBoundsException("toIndex = " + toIndex);
        if (fromIndex > toIndex) throw new IllegalArgumentException("fromIndex(" + fromIndex + ") > toIndex(" + toIndex + ")");
        return new SubList<E>(this, fromIndex, toIndex);
    }

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof List)) return false;
        ListIterator<E> e1 = listIterator();
        ListIterator<?> e2 = ((List<?>) o).listIterator();
        while (e1.hasNext() && e2.hasNext()) {
            if (!Objects.equals(e1.next(), e2.next())) return false;
        }
        return !(e1.hasNext() || e2.hasNext());
    }

    public int hashCode() {
        int h = 1;
        for (E e : this) h = 31 * h + (e == null ? 0 : e.hashCode());
        return h;
    }

    private static final class SubList<E> extends AbstractList<E> {
        private final AbstractList<E> root;
        private final int offset;
        private int size;

        SubList(AbstractList<E> root, int from, int to) {
            this.root = root;
            this.offset = from;
            this.size = to - from;
            this.modCount = root.modCount;
        }

        private void check(int index) {
            if (index < 0 || index >= size) throw new IndexOutOfBoundsException("Index: " + index + ", Size: " + size);
            if (root.modCount != modCount) throw new ConcurrentModificationException();
        }

        public E get(int index) {
            check(index);
            return root.get(offset + index);
        }

        public E set(int index, E e) {
            check(index);
            return root.set(offset + index, e);
        }

        public int size() {
            if (root.modCount != modCount) throw new ConcurrentModificationException();
            return size;
        }

        public void add(int index, E e) {
            checkPositionIndex(index, size);
            if (root.modCount != modCount) throw new ConcurrentModificationException();
            root.add(offset + index, e);
            modCount = root.modCount;
            size++;
        }

        public E remove(int index) {
            check(index);
            E r = root.remove(offset + index);
            modCount = root.modCount;
            size--;
            return r;
        }
    }
}
