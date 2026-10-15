package java.util.concurrent;

import java.util.Queue;

public interface BlockingQueue<E> extends Queue<E> {
    void put(E e) throws InterruptedException;

    E take() throws InterruptedException;

    boolean offer(E e, long timeout, TimeUnit unit) throws InterruptedException;

    E poll(long timeout, TimeUnit unit) throws InterruptedException;
}
