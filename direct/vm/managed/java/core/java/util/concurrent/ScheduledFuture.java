package java.util.concurrent;

public interface ScheduledFuture<V> extends Future<V> {
    long getDelay(TimeUnit unit);
}
