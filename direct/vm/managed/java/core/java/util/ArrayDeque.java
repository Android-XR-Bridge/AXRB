package java.util;

public class ArrayDeque<E> extends AbstractCollection<E> implements Deque<E>, Cloneable, java.io.Serializable {
    private Object[] elements;
    private int head;
    private int size;
    private int modCount;

    public ArrayDeque() {
        elements = new Object[16];
    }

    public ArrayDeque(int numElements) {
        elements = new Object[Math.max(numElements, 1)];
    }

    public ArrayDeque(Collection<? extends E> c) {
        this(c.size());
        addAll(c);
    }

    private void grow() {
        Object[] a = new Object[elements.length * 2];
        for (int i = 0; i < size; i++) a[i] = elements[(head + i) % elements.length];
        elements = a;
        head = 0;
    }

    private int index(int i) {
        return (head + i) % elements.length;
    }

    public void addFirst(E e) {
        if (e == null) throw new NullPointerException();
        if (size == elements.length) grow();
        head = (head - 1 + elements.length) % elements.length;
        elements[head] = e;
        size++;
        modCount++;
    }

    public void addLast(E e) {
        if (e == null) throw new NullPointerException();
        if (size == elements.length) grow();
        elements[index(size)] = e;
        size++;
        modCount++;
    }

    public boolean offerFirst(E e) {
        addFirst(e);
        return true;
    }

    public boolean offerLast(E e) {
        addLast(e);
        return true;
    }

    public E removeFirst() {
        E e = pollFirst();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    public E removeLast() {
        E e = pollLast();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    @SuppressWarnings("unchecked")
    public E pollFirst() {
        if (size == 0) return null;
        E e = (E) elements[head];
        elements[head] = null;
        head = (head + 1) % elements.length;
        size--;
        modCount++;
        return e;
    }

    @SuppressWarnings("unchecked")
    public E pollLast() {
        if (size == 0) return null;
        int i = index(size - 1);
        E e = (E) elements[i];
        elements[i] = null;
        size--;
        modCount++;
        return e;
    }

    public E getFirst() {
        E e = peekFirst();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    public E getLast() {
        E e = peekLast();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    @SuppressWarnings("unchecked")
    public E peekFirst() {
        return size == 0 ? null : (E) elements[head];
    }

    @SuppressWarnings("unchecked")
    public E peekLast() {
        return size == 0 ? null : (E) elements[index(size - 1)];
    }

    public boolean add(E e) {
        addLast(e);
        return true;
    }

    public boolean offer(E e) {
        return offerLast(e);
    }

    public E remove() {
        return removeFirst();
    }

    public E poll() {
        return pollFirst();
    }

    public E element() {
        return getFirst();
    }

    public E peek() {
        return peekFirst();
    }

    public void push(E e) {
        addFirst(e);
    }

    public E pop() {
        return removeFirst();
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    public void clear() {
        for (int i = 0; i < size; i++) elements[index(i)] = null;
        head = 0;
        size = 0;
        modCount++;
    }

    private void removeAt(int i) {
        for (int k = i; k < size - 1; k++) elements[index(k)] = elements[index(k + 1)];
        elements[index(size - 1)] = null;
        size--;
        modCount++;
    }

    public Iterator<E> iterator() {
        return new Itr(false);
    }

    public Iterator<E> descendingIterator() {
        return new Itr(true);
    }

    private final class Itr implements Iterator<E> {
        private final boolean descending;
        private int next;  // logical position of the next element
        private int last = -1;
        private int expected = modCount;

        Itr(boolean descending) {
            this.descending = descending;
            this.next = descending ? size - 1 : 0;
        }

        public boolean hasNext() {
            return descending ? next >= 0 : next < size;
        }

        @SuppressWarnings("unchecked")
        public E next() {
            if (modCount != expected) throw new ConcurrentModificationException();
            if (!hasNext()) throw new NoSuchElementException();
            last = next;
            next += descending ? -1 : 1;
            return (E) elements[index(last)];
        }

        public void remove() {
            if (last < 0) throw new IllegalStateException();
            if (modCount != expected) throw new ConcurrentModificationException();
            removeAt(last);
            if (!descending) next = last;
            last = -1;
            expected = modCount;
        }
    }

    @SuppressWarnings("unchecked")
    public ArrayDeque<E> clone() {
        try {
            ArrayDeque<E> r = (ArrayDeque<E>) super.clone();
            r.elements = elements.clone();
            return r;
        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
    }
}
