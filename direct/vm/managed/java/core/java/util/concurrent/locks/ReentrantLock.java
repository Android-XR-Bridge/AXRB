package java.util.concurrent.locks;

import java.util.concurrent.TimeUnit;

/**
 * A reentrant mutual-exclusion lock built on the monitor of the lock object.
 * Conditions wait on that monitor too; signal() wakes every waiter of the lock
 * (spurious wake-ups are permitted by the Condition contract).
 */
public class ReentrantLock implements Lock, java.io.Serializable {
    private Thread owner;
    private int holds;

    public ReentrantLock() {}

    public ReentrantLock(boolean fair) {}

    public void lock() {
        boolean interrupted = false;
        Thread self = Thread.currentThread();
        synchronized (this) {
            while (owner != null && owner != self) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    interrupted = true;
                }
            }
            owner = self;
            holds++;
        }
        if (interrupted) self.interrupt();
    }

    public void lockInterruptibly() throws InterruptedException {
        Thread self = Thread.currentThread();
        synchronized (this) {
            while (owner != null && owner != self) wait();
            owner = self;
            holds++;
        }
    }

    public synchronized boolean tryLock() {
        Thread self = Thread.currentThread();
        if (owner != null && owner != self) return false;
        owner = self;
        holds++;
        return true;
    }

    public synchronized boolean tryLock(long time, TimeUnit unit) throws InterruptedException {
        Thread self = Thread.currentThread();
        long deadline = System.nanoTime() + unit.toNanos(time);
        while (owner != null && owner != self) {
            long left = deadline - System.nanoTime();
            if (left <= 0) return false;
            wait(Math.max(1L, left / 1000000L));
        }
        owner = self;
        holds++;
        return true;
    }

    public synchronized void unlock() {
        if (owner != Thread.currentThread()) throw new IllegalMonitorStateException();
        if (--holds == 0) {
            owner = null;
            notifyAll();
        }
    }

    public synchronized boolean isLocked() {
        return owner != null;
    }

    public synchronized boolean isHeldByCurrentThread() {
        return owner == Thread.currentThread();
    }

    public synchronized int getHoldCount() {
        return owner == Thread.currentThread() ? holds : 0;
    }

    public boolean isFair() {
        return false;
    }

    public Condition newCondition() {
        return new ConditionObject();
    }

    private final class ConditionObject implements Condition {
        private int signals;

        /** Releases the lock completely, waits, and re-acquires it. Returns the nanoseconds left. */
        private long block(long nanos, boolean interruptible) throws InterruptedException {
            ReentrantLock l = ReentrantLock.this;
            Thread self = Thread.currentThread();
            long start = System.nanoTime();
            InterruptedException interrupted = null;
            synchronized (l) {
                if (l.owner != self) throw new IllegalMonitorStateException();
                int saved = l.holds;
                int seen = signals;
                l.owner = null;
                l.holds = 0;
                l.notifyAll();
                try {
                    while (signals == seen) {
                        if (nanos >= 0) {
                            long left = nanos - (System.nanoTime() - start);
                            if (left <= 0) break;
                            l.wait(Math.max(1L, left / 1000000L));
                        } else {
                            l.wait();
                        }
                    }
                } catch (InterruptedException e) {
                    if (interruptible) interrupted = e;
                    else self.interrupt();
                }
                boolean reinterrupt = false;
                while (l.owner != null) {
                    try {
                        l.wait();
                    } catch (InterruptedException e) {
                        reinterrupt = true;
                    }
                }
                l.owner = self;
                l.holds = saved;
                if (reinterrupt) self.interrupt();
            }
            if (interrupted != null) throw interrupted;
            return nanos < 0 ? 0 : nanos - (System.nanoTime() - start);
        }

        public void await() throws InterruptedException {
            block(-1, true);
        }

        public void awaitUninterruptibly() {
            try {
                block(-1, false);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        public long awaitNanos(long nanosTimeout) throws InterruptedException {
            return block(Math.max(0L, nanosTimeout), true);
        }

        public boolean await(long time, TimeUnit unit) throws InterruptedException {
            return block(Math.max(0L, unit.toNanos(time)), true) > 0;
        }

        public void signal() {
            signalAll();
        }

        public void signalAll() {
            ReentrantLock l = ReentrantLock.this;
            synchronized (l) {
                if (l.owner != Thread.currentThread()) throw new IllegalMonitorStateException();
                signals++;
                l.notifyAll();
            }
        }
    }
}
