package java.util.concurrent;

import java.util.concurrent.atomic.AtomicInteger;

public class Executors {
    private static final AtomicInteger POOLS = new AtomicInteger(1);

    private Executors() {}

    public static ExecutorService newFixedThreadPool(int nThreads) {
        return new ThreadPoolExecutor(nThreads, nThreads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>());
    }

    public static ExecutorService newFixedThreadPool(int nThreads, ThreadFactory threadFactory) {
        return new ThreadPoolExecutor(nThreads, nThreads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>(), threadFactory);
    }

    public static ExecutorService newSingleThreadExecutor() {
        return newFixedThreadPool(1);
    }

    public static ExecutorService newSingleThreadExecutor(ThreadFactory threadFactory) {
        return newFixedThreadPool(1, threadFactory);
    }

    public static ExecutorService newCachedThreadPool() {
        return new ThreadPoolExecutor(0, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS,
                new LinkedBlockingQueue<Runnable>());
    }

    public static ExecutorService newCachedThreadPool(ThreadFactory threadFactory) {
        return new ThreadPoolExecutor(0, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS,
                new LinkedBlockingQueue<Runnable>(), threadFactory);
    }

    public static ScheduledExecutorService newScheduledThreadPool(int corePoolSize) {
        return new ScheduledThreadPoolExecutor(corePoolSize);
    }

    public static ScheduledExecutorService newScheduledThreadPool(int corePoolSize, ThreadFactory threadFactory) {
        return new ScheduledThreadPoolExecutor(corePoolSize, threadFactory);
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor() {
        return new ScheduledThreadPoolExecutor(1);
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor(ThreadFactory threadFactory) {
        return new ScheduledThreadPoolExecutor(1, threadFactory);
    }

    public static ExecutorService unconfigurableExecutorService(ExecutorService executor) {
        if (executor == null) throw new NullPointerException();
        return executor;
    }

    public static ScheduledExecutorService unconfigurableScheduledExecutorService(ScheduledExecutorService executor) {
        if (executor == null) throw new NullPointerException();
        return executor;
    }

    public static <T> Callable<T> callable(final Runnable task, final T result) {
        return new Callable<T>() {
            public T call() {
                task.run();
                return result;
            }
        };
    }

    public static ThreadFactory defaultThreadFactory() {
        final int pool = POOLS.getAndIncrement();
        return new ThreadFactory() {
            private final AtomicInteger number = new AtomicInteger(1);

            public Thread newThread(Runnable r) {
                Thread t = new Thread(r, "pool-" + pool + "-thread-" + number.getAndIncrement());
                if (t.isDaemon()) t.setDaemon(false);
                return t;
            }
        };
    }
}
