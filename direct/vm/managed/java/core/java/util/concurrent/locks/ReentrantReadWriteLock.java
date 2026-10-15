package java.util.concurrent.locks;

import java.util.concurrent.TimeUnit;

/**
 * Readers and the writer share one reentrant lock: correct, but readers do
 * not run concurrently with each other.
 */
public class ReentrantReadWriteLock implements ReadWriteLock, java.io.Serializable {
    final ReentrantLock lock = new ReentrantLock();
    private final ReadLock readerLock = new ReadLock(this);
    private final WriteLock writerLock = new WriteLock(this);

    public ReentrantReadWriteLock() {}

    public ReentrantReadWriteLock(boolean fair) {}

    public ReentrantReadWriteLock.ReadLock readLock() {
        return readerLock;
    }

    public ReentrantReadWriteLock.WriteLock writeLock() {
        return writerLock;
    }

    abstract static class Shared implements Lock, java.io.Serializable {
        private final ReentrantLock lock;

        Shared(ReentrantReadWriteLock owner) {
            lock = owner.lock;
        }

        public void lock() {
            lock.lock();
        }

        public void lockInterruptibly() throws InterruptedException {
            lock.lockInterruptibly();
        }

        public boolean tryLock() {
            return lock.tryLock();
        }

        public boolean tryLock(long time, TimeUnit unit) throws InterruptedException {
            return lock.tryLock(time, unit);
        }

        public void unlock() {
            lock.unlock();
        }

        public Condition newCondition() {
            return lock.newCondition();
        }
    }

    public static class ReadLock extends Shared {
        protected ReadLock(ReentrantReadWriteLock lock) {
            super(lock);
        }

        public Condition newCondition() {
            throw new UnsupportedOperationException();
        }
    }

    public static class WriteLock extends Shared {
        protected WriteLock(ReentrantReadWriteLock lock) {
            super(lock);
        }
    }
}
