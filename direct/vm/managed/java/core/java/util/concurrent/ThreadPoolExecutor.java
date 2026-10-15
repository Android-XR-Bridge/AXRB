package java.util.concurrent;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * A pool of up to maximumPoolSize worker threads fed from one queue. Idle
 * workers above the core size exit after the keep-alive time.
 */
public class ThreadPoolExecutor extends AbstractExecutorService {
    private final Object lock = new Object();
    private final ArrayDeque<Runnable> queue = new ArrayDeque<Runnable>();
    private final ArrayList<Thread> workers = new ArrayList<Thread>();
    private int corePoolSize;
    private int maximumPoolSize;
    private long keepAliveNanos;
    private ThreadFactory threadFactory;
    private int idle;
    private boolean shutdown;

    public ThreadPoolExecutor(int corePoolSize, int maximumPoolSize, long keepAliveTime, TimeUnit unit,
            BlockingQueue<Runnable> workQueue) {
        this(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue, Executors.defaultThreadFactory());
    }

    public ThreadPoolExecutor(int corePoolSize, int maximumPoolSize, long keepAliveTime, TimeUnit unit,
            BlockingQueue<Runnable> workQueue, ThreadFactory threadFactory) {
        if (corePoolSize < 0 || maximumPoolSize <= 0 || maximumPoolSize < corePoolSize)
            throw new IllegalArgumentException();
        this.corePoolSize = corePoolSize;
        this.maximumPoolSize = maximumPoolSize;
        this.keepAliveNanos = unit.toNanos(keepAliveTime);
        this.threadFactory = threadFactory;
    }

    public void execute(Runnable command) {
        if (command == null) throw new NullPointerException();
        synchronized (lock) {
            if (shutdown) throw new RejectedExecutionException("executor has been shut down");
            queue.addLast(command);
            if (idle > 0) {
                lock.notify();
                return;
            }
            if (workers.size() < maximumPoolSize) startWorker();
        }
    }

    private void startWorker() {
        Thread t = threadFactory.newThread(new Runnable() {
            public void run() {
                work();
            }
        });
        workers.add(t);
        t.start();
    }

    private void work() {
        for (;;) {
            Runnable task;
            synchronized (lock) {
                long deadline = System.nanoTime() + keepAliveNanos;
                while (queue.isEmpty()) {
                    if (shutdown) {
                        exit();
                        return;
                    }
                    boolean timed = workers.size() > corePoolSize;
                    long left = deadline - System.nanoTime();
                    if (timed && left <= 0) {
                        exit();
                        return;
                    }
                    idle++;
                    try {
                        lock.wait(timed ? Math.max(1L, left / 1000000L) : 0L);
                    } catch (InterruptedException e) {
                        // shutdownNow or a stray interrupt: re-check the state
                    } finally {
                        idle--;
                    }
                }
                task = queue.pollFirst();
            }
            try {
                task.run();
            } catch (Throwable t) {
                Thread.UncaughtExceptionHandler h = Thread.getDefaultUncaughtExceptionHandler();
                if (h != null) h.uncaughtException(Thread.currentThread(), t);
                else t.printStackTrace();
            }
        }
    }

    private void exit() {
        workers.remove(Thread.currentThread());
        lock.notifyAll();
    }

    public void shutdown() {
        synchronized (lock) {
            shutdown = true;
            lock.notifyAll();
        }
    }

    public List<Runnable> shutdownNow() {
        synchronized (lock) {
            shutdown = true;
            ArrayList<Runnable> pending = new ArrayList<Runnable>(queue);
            queue.clear();
            for (Thread t : workers) t.interrupt();
            lock.notifyAll();
            return pending;
        }
    }

    public boolean isShutdown() {
        synchronized (lock) {
            return shutdown;
        }
    }

    public boolean isTerminated() {
        synchronized (lock) {
            return shutdown && workers.isEmpty();
        }
    }

    public boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        synchronized (lock) {
            while (!(shutdown && workers.isEmpty())) {
                long left = deadline - System.nanoTime();
                if (left <= 0) return false;
                lock.wait(Math.max(1L, left / 1000000L));
            }
            return true;
        }
    }

    public int getCorePoolSize() {
        return corePoolSize;
    }

    public void setCorePoolSize(int corePoolSize) {
        synchronized (lock) {
            this.corePoolSize = corePoolSize;
            lock.notifyAll();
        }
    }

    public int getMaximumPoolSize() {
        return maximumPoolSize;
    }

    public void setMaximumPoolSize(int maximumPoolSize) {
        synchronized (lock) {
            this.maximumPoolSize = maximumPoolSize;
        }
    }

    public void allowCoreThreadTimeOut(boolean value) {}

    public void setThreadFactory(ThreadFactory threadFactory) {
        this.threadFactory = threadFactory;
    }
}
