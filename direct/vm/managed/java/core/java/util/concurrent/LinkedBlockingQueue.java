package java.util.concurrent;

import java.util.AbstractCollection;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.NoSuchElementException;

public class LinkedBlockingQueue<E> extends AbstractCollection<E> implements BlockingQueue<E>, java.io.Serializable {
    private final ArrayDeque<E> items = new ArrayDeque<E>();
    private final int capacity;

    public LinkedBlockingQueue() {
        this(Integer.MAX_VALUE);
    }

    public LinkedBlockingQueue(int capacity) {
        if (capacity <= 0) throw new IllegalArgumentException();
        this.capacity = capacity;
    }

    public synchronized int size() {
        return items.size();
    }

    public synchronized boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        if (items.size() >= capacity) return false;
        items.addLast(e);
        notifyAll();
        return true;
    }

    public boolean add(E e) {
        if (!offer(e)) throw new IllegalStateException("Queue full");
        return true;
    }

    public synchronized void put(E e) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        while (items.size() >= capacity) wait();
        items.addLast(e);
        notifyAll();
    }

    public synchronized boolean offer(E e, long timeout, TimeUnit unit) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (items.size() >= capacity) {
            long left = deadline - System.nanoTime();
            if (left <= 0) return false;
            wait(Math.max(1L, left / 1000000L));
        }
        items.addLast(e);
        notifyAll();
        return true;
    }

    public synchronized E take() throws InterruptedException {
        while (items.isEmpty()) wait();
        E e = items.pollFirst();
        notifyAll();
        return e;
    }

    public synchronized E poll(long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (items.isEmpty()) {
            long left = deadline - System.nanoTime();
            if (left <= 0) return null;
            wait(Math.max(1L, left / 1000000L));
        }
        E e = items.pollFirst();
        notifyAll();
        return e;
    }

    public synchronized E poll() {
        E e = items.pollFirst();
        if (e != null) notifyAll();
        return e;
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

    public synchronized void clear() {
        items.clear();
        notifyAll();
    }

    public synchronized Iterator<E> iterator() {
        return new ArrayList<E>(items).iterator();
    }
}
