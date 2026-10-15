package java.util.concurrent;

public class CountDownLatch {
    private long count;

    public CountDownLatch(int count) {
        if (count < 0) throw new IllegalArgumentException("count < 0");
        this.count = count;
    }

    public synchronized void await() throws InterruptedException {
        while (count > 0) wait();
    }

    public synchronized boolean await(long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (count > 0) {
            long left = deadline - System.nanoTime();
            if (left <= 0) return false;
            wait(Math.max(1L, left / 1000000L));
        }
        return true;
    }

    public synchronized void countDown() {
        if (count > 0 && --count == 0) notifyAll();
    }

    public synchronized long getCount() {
        return count;
    }
}
