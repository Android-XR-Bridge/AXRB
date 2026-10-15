package java.util;

/** Array-backed: same contract as a linked list, different complexity. */
public class LinkedList<E> extends ArrayList<E> implements Deque<E> {
    public LinkedList() {
        super();
    }

    public LinkedList(Collection<? extends E> c) {
        super(c);
    }

    public void addFirst(E e) {
        add(0, e);
    }

    public void addLast(E e) {
        add(e);
    }

    public boolean offerFirst(E e) {
        addFirst(e);
        return true;
    }

    public boolean offerLast(E e) {
        return add(e);
    }

    public boolean offer(E e) {
        return add(e);
    }

    public E removeFirst() {
        if (isEmpty()) throw new NoSuchElementException();
        return remove(0);
    }

    public E removeLast() {
        if (isEmpty()) throw new NoSuchElementException();
        return remove(size() - 1);
    }

    public E pollFirst() {
        return isEmpty() ? null : remove(0);
    }

    public E pollLast() {
        return isEmpty() ? null : remove(size() - 1);
    }

    public E getFirst() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(0);
    }

    public E getLast() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(size() - 1);
    }

    public E peekFirst() {
        return isEmpty() ? null : get(0);
    }

    public E peekLast() {
        return isEmpty() ? null : get(size() - 1);
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

    public boolean removeFirstOccurrence(Object o) {
        return remove(o);
    }

    public boolean removeLastOccurrence(Object o) {
        int i = lastIndexOf(o);
        if (i < 0) return false;
        remove(i);
        return true;
    }

    public Iterator<E> descendingIterator() {
        return new Iterator<E>() {
            int next = size() - 1;
            int last = -1;

            public boolean hasNext() {
                return next >= 0;
            }

            public E next() {
                if (next < 0) throw new NoSuchElementException();
                last = next;
                return get(next--);
            }

            public void remove() {
                if (last < 0) throw new IllegalStateException();
                LinkedList.this.remove(last);
                last = -1;
            }
        };
    }
}
