package java.util.concurrent;

public class FutureTask<V> implements RunnableFuture<V> {
    private static final int NEW = 0, RUNNING = 1, DONE = 2, FAILED = 3, CANCELLED = 4;

    private Callable<V> callable;
    private int state = NEW;
    private V result;
    private Throwable failure;
    private Thread runner;

    public FutureTask(Callable<V> callable) {
        if (callable == null) throw new NullPointerException();
        this.callable = callable;
    }

    public FutureTask(final Runnable runnable, final V result) {
        if (runnable == null) throw new NullPointerException();
        this.callable = new Callable<V>() {
            public V call() {
                runnable.run();
                return result;
            }
        };
    }

    public void run() {
        Callable<V> c;
        synchronized (this) {
            if (state != NEW) return;
            state = RUNNING;
            runner = Thread.currentThread();
            c = callable;
        }
        V v = null;
        Throwable t = null;
        try {
            v = c.call();
        } catch (Throwable e) {
            t = e;
        }
        synchronized (this) {
            runner = null;
            if (state == RUNNING) {
                if (t != null) {
                    failure = t;
                    state = FAILED;
                } else {
                    result = v;
                    state = DONE;
                }
            }
            callable = null;
            notifyAll();
        }
        done();
    }

    /** Runs without setting a result; false once failed or cancelled. */
    protected boolean runAndReset() {
        Callable<V> c;
        synchronized (this) {
            if (state != NEW) return false;
            state = RUNNING;
            runner = Thread.currentThread();
            c = callable;
        }
        Throwable t = null;
        try {
            c.call();
        } catch (Throwable e) {
            t = e;
        }
        synchronized (this) {
            runner = null;
            if (state != RUNNING) return false;
            if (t != null) {
                failure = t;
                state = FAILED;
                notifyAll();
                return false;
            }
            state = NEW;
            return true;
        }
    }

    protected void done() {}

    protected void set(V v) {
        synchronized (this) {
            if (state > RUNNING) return;
            result = v;
            state = DONE;
            notifyAll();
        }
        done();
    }

    protected void setException(Throwable t) {
        synchronized (this) {
            if (state > RUNNING) return;
            failure = t;
            state = FAILED;
            notifyAll();
        }
        done();
    }

    public boolean cancel(boolean mayInterruptIfRunning) {
        synchronized (this) {
            if (state > RUNNING) return false;
            if (state == RUNNING && mayInterruptIfRunning && runner != null) runner.interrupt();
            state = CANCELLED;
            notifyAll();
        }
        done();
        return true;
    }

    public synchronized boolean isCancelled() {
        return state == CANCELLED;
    }

    public synchronized boolean isDone() {
        return state > RUNNING;
    }

    private V report() throws ExecutionException {
        if (state == CANCELLED) throw new CancellationException();
        if (state == FAILED) throw new ExecutionException(failure);
        return result;
    }

    public synchronized V get() throws InterruptedException, ExecutionException {
        while (state <= RUNNING) wait();
        return report();
    }

    public synchronized V get(long timeout, TimeUnit unit)
            throws InterruptedException, ExecutionException, TimeoutException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (state <= RUNNING) {
            long left = deadline - System.nanoTime();
            if (left <= 0) throw new TimeoutException();
            wait(Math.max(1L, left / 1000000L));
        }
        return report();
    }
}
