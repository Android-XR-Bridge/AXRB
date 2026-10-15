package java.util.concurrent;

public abstract class AbstractExecutorService implements ExecutorService {
    protected <T> RunnableFuture<T> newTaskFor(Runnable runnable, T value) {
        return new FutureTask<T>(runnable, value);
    }

    protected <T> RunnableFuture<T> newTaskFor(Callable<T> callable) {
        return new FutureTask<T>(callable);
    }

    public Future<?> submit(Runnable task) {
        RunnableFuture<Object> f = newTaskFor(task, null);
        execute(f);
        return f;
    }

    public <T> Future<T> submit(Runnable task, T result) {
        RunnableFuture<T> f = newTaskFor(task, result);
        execute(f);
        return f;
    }

    public <T> Future<T> submit(Callable<T> task) {
        RunnableFuture<T> f = newTaskFor(task);
        execute(f);
        return f;
    }
}
