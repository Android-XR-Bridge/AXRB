package java.util.concurrent;

public class Semaphore implements java.io.Serializable {
    private int permits;

    public Semaphore(int permits) {
        this.permits = permits;
    }

    public Semaphore(int permits, boolean fair) {
        this.permits = permits;
    }

    public void acquire() throws InterruptedException {
        acquire(1);
    }

    public synchronized void acquire(int n) throws InterruptedException {
        if (n < 0) throw new IllegalArgumentException();
        while (permits < n) wait();
        permits -= n;
    }

    public void acquireUninterruptibly() {
        boolean interrupted = false;
        synchronized (this) {
            while (permits < 1) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    interrupted = true;
                }
            }
            permits--;
        }
        if (interrupted) Thread.currentThread().interrupt();
    }

    public synchronized boolean tryAcquire() {
        if (permits < 1) return false;
        permits--;
        return true;
    }

    public synchronized boolean tryAcquire(int n) {
        if (permits < n) return false;
        permits -= n;
        return true;
    }

    public boolean tryAcquire(long timeout, TimeUnit unit) throws InterruptedException {
        return tryAcquire(1, timeout, unit);
    }

    public synchronized boolean tryAcquire(int n, long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (permits < n) {
            long left = deadline - System.nanoTime();
            if (left <= 0) return false;
            wait(Math.max(1L, left / 1000000L));
        }
        permits -= n;
        return true;
    }

    public void release() {
        release(1);
    }

    public synchronized void release(int n) {
        if (n < 0) throw new IllegalArgumentException();
        permits += n;
        notifyAll();
    }

    public synchronized int availablePermits() {
        return permits;
    }

    public synchronized int drainPermits() {
        int n = permits;
        permits = 0;
        return n;
    }
}
