package java.lang.ref;

import java.util.ArrayDeque;

public class ReferenceQueue<T> {
    private final ArrayDeque<Reference<? extends T>> queue = new ArrayDeque<Reference<? extends T>>();

    public ReferenceQueue() {}

    synchronized boolean enqueue(Reference<? extends T> r) {
        if (r.enqueued) return false;
        r.enqueued = true;
        queue.addLast(r);
        notifyAll();
        return true;
    }

    public synchronized Reference<? extends T> poll() {
        return queue.pollFirst();
    }

    public synchronized Reference<? extends T> remove(long timeout) throws InterruptedException {
        if (timeout < 0) throw new IllegalArgumentException("Negative timeout value");
        long deadline = System.nanoTime() / 1000000L + timeout;
        while (queue.isEmpty()) {
            if (timeout == 0) {
                wait();
            } else {
                long left = deadline - System.nanoTime() / 1000000L;
                if (left <= 0) return null;
                wait(left);
            }
        }
        return queue.pollFirst();
    }

    public Reference<? extends T> remove() throws InterruptedException {
        return remove(0);
    }
}
