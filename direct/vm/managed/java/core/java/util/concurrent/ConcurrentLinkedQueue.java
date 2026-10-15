package java.util.concurrent;

import java.util.AbstractCollection;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Queue;

/** Lock-based; iterators work on a snapshot (weakly consistent, as specified). */
public class ConcurrentLinkedQueue<E> extends AbstractCollection<E> implements Queue<E>, java.io.Serializable {
    private final ArrayDeque<E> items = new ArrayDeque<E>();

    public ConcurrentLinkedQueue() {}

    public ConcurrentLinkedQueue(Collection<? extends E> c) {
        for (E e : c) offer(e);
    }

    public synchronized boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        items.addLast(e);
        return true;
    }

    public boolean add(E e) {
        return offer(e);
    }

    public synchronized E poll() {
        return items.pollFirst();
    }

    public synchronized E peek() {
        return items.peekFirst();
    }

    public E remove() {
        E e = poll();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    public E element() {
        E e = peek();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    public synchronized boolean remove(Object o) {
        return items.remove(o);
    }

    public synchronized int size() {
        return items.size();
    }

    public synchronized boolean isEmpty() {
        return items.isEmpty();
    }

    public synchronized void clear() {
        items.clear();
    }

    public synchronized Iterator<E> iterator() {
        return new ArrayList<E>(items).iterator();
    }
}
