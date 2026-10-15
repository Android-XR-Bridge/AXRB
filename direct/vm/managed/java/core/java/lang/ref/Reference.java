package java.lang.ref;

/**
 * The garbage collector treats `referent` as weak and clears it when the
 * referent is otherwise unreachable. Soft references are cleared as eagerly as
 * weak ones. Reference queues are never enqueued by the collector (documented
 * gap in docs/managed-runtime.md); enqueue() works when called explicitly.
 */
public abstract class Reference<T> {
    volatile T referent;
    final ReferenceQueue<? super T> queue;
    boolean enqueued;

    Reference(T referent) {
        this(referent, null);
    }

    Reference(T referent, ReferenceQueue<? super T> queue) {
        this.referent = referent;
        this.queue = queue;
    }

    public T get() {
        return referent;
    }

    public void clear() {
        referent = null;
    }

    public boolean isEnqueued() {
        return enqueued;
    }

    public boolean enqueue() {
        clear();
        return queue != null && queue.enqueue(this);
    }
}
