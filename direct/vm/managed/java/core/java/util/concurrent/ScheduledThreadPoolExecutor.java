package java.util.concurrent;

import java.util.ArrayList;
import java.util.List;

/** Worker threads take the earliest due task from one list ordered by time. */
public class ScheduledThreadPoolExecutor extends AbstractExecutorService implements ScheduledExecutorService {
    private final Object lock = new Object();
    private final ArrayList<Task<?>> queue = new ArrayList<Task<?>>();
    private final ArrayList<Thread> workers = new ArrayList<Thread>();
    private final int corePoolSize;
    private final ThreadFactory threadFactory;
    private boolean shutdown;
    private int running;

    public ScheduledThreadPoolExecutor(int corePoolSize) {
        this(corePoolSize, Executors.defaultThreadFactory());
    }

    public ScheduledThreadPoolExecutor(int corePoolSize, ThreadFactory threadFactory) {
        this.corePoolSize = Math.max(corePoolSize, 1);
        this.threadFactory = threadFactory;
    }

    private final class Task<V> extends FutureTask<V> implements ScheduledFuture<V> {
        long time;
        final long period; // > 0 fixed rate, < 0 fixed delay, 0 one shot

        Task(Callable<V> callable, long time) {
            super(callable);
            this.time = time;
            this.period = 0;
        }

        Task(Runnable runnable, long time, long period) {
            super(runnable, null);
            this.time = time;
            this.period = period;
        }

        public long getDelay(TimeUnit unit) {
            return unit.convert(time - System.nanoTime(), TimeUnit.NANOSECONDS);
        }

        public boolean cancel(boolean mayInterruptIfRunning) {
            boolean cancelled = super.cancel(mayInterruptIfRunning);
            synchronized (lock) {
                queue.remove(this);
            }
            return cancelled;
        }

        public void run() {
            if (period == 0) {
                super.run();
            } else if (runAndReset()) {
                time = period > 0 ? time + period : System.nanoTime() - period;
                enqueue(this);
            }
        }
    }

    private void enqueue(Task<?> task) {
        synchronized (lock) {
            if (shutdown) {
                if (task.period != 0) return;
                throw new RejectedExecutionException("executor has been shut down");
            }
            int i = 0;
            while (i < queue.size() && queue.get(i).time <= task.time) i++;
            queue.add(i, task);
            if (workers.size() < corePoolSize) {
                Thread t = threadFactory.newThread(new Runnable() {
                    public void run() {
                        work();
                    }
                });
                workers.add(t);
                t.start();
            }
            lock.notifyAll();
        }
    }

    private void work() {
        for (;;) {
            Task<?> task;
            synchronized (lock) {
                for (;;) {
                    if (shutdown && queue.isEmpty()) {
                        workers.remove(Thread.currentThread());
                        lock.notifyAll();
                        return;
                    }
                    if (!queue.isEmpty()) {
                        long wait = queue.get(0).time - System.nanoTime();
                        if (wait <= 0) {
                            task = queue.remove(0);
                            break;
                        }
                        waitNanos(wait);
                    } else {
                        waitNanos(0);
                    }
                }
                running++;
            }
            try {
                task.run();
            } catch (Throwable ignored) {
            } finally {
                synchronized (lock) {
                    running--;
                }
            }
        }
    }

    private void waitNanos(long nanos) {
        try {
            if (nanos <= 0) lock.wait();
            else lock.wait(Math.max(1L, nanos / 1000000L));
        } catch (InterruptedException ignored) {
        }
    }

    private static long due(long delay, TimeUnit unit) {
        return System.nanoTime() + unit.toNanos(Math.max(delay, 0));
    }

    public ScheduledFuture<?> schedule(Runnable command, long delay, TimeUnit unit) {
        Task<Void> t = new Task<Void>(command, due(delay, unit), 0);
        enqueue(t);
        return t;
    }

    public <V> ScheduledFuture<V> schedule(Callable<V> callable, long delay, TimeUnit unit) {
        Task<V> t = new Task<V>(callable, due(delay, unit));
        enqueue(t);
        return t;
    }

    public ScheduledFuture<?> scheduleAtFixedRate(Runnable command, long initialDelay, long period, TimeUnit unit) {
        if (period <= 0) throw new IllegalArgumentException();
        Task<Void> t = new Task<Void>(command, due(initialDelay, unit), unit.toNanos(period));
        enqueue(t);
        return t;
    }

    public ScheduledFuture<?> scheduleWithFixedDelay(Runnable command, long initialDelay, long delay, TimeUnit unit) {
        if (delay <= 0) throw new IllegalArgumentException();
        Task<Void> t = new Task<Void>(command, due(initialDelay, unit), -unit.toNanos(delay));
        enqueue(t);
        return t;
    }

    public void execute(Runnable command) {
        schedule(command, 0, TimeUnit.NANOSECONDS);
    }

    public void shutdown() {
        synchronized (lock) {
            shutdown = true;
            for (int i = queue.size() - 1; i >= 0; i--) {
                if (queue.get(i).period != 0) queue.remove(i);
            }
            lock.notifyAll();
        }
    }

    public List<Runnable> shutdownNow() {
        synchronized (lock) {
            shutdown = true;
            List<Runnable> pending = new ArrayList<Runnable>(queue);
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
}
