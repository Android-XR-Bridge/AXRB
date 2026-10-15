package android.os;

public class Handler {
    public interface Callback {
        boolean handleMessage(Message msg);
    }

    final Looper looper;
    final MessageQueue queue;
    final Callback callback;

    public Handler() {
        this((Callback) null);
    }

    public Handler(Callback callback) {
        looper = Looper.myLooper();
        if (looper == null) {
            throw new RuntimeException("Can't create handler inside thread " + Thread.currentThread()
                    + " that has not called Looper.prepare()");
        }
        queue = looper.queue;
        this.callback = callback;
    }

    public Handler(Looper looper) {
        this(looper, null);
    }

    public Handler(Looper looper, Callback callback) {
        if (looper == null) throw new NullPointerException("looper == null");
        this.looper = looper;
        this.queue = looper.queue;
        this.callback = callback;
    }

    public void handleMessage(Message msg) {}

    public void dispatchMessage(Message msg) {
        if (msg.callback != null) {
            msg.callback.run();
        } else {
            if (callback != null && callback.handleMessage(msg)) return;
            handleMessage(msg);
        }
    }

    public final Looper getLooper() {
        return looper;
    }

    public final Message obtainMessage() {
        return Message.obtain(this);
    }

    public final Message obtainMessage(int what) {
        return Message.obtain(this, what);
    }

    public final Message obtainMessage(int what, Object obj) {
        return Message.obtain(this, what, obj);
    }

    public final Message obtainMessage(int what, int arg1, int arg2) {
        return Message.obtain(this, what, arg1, arg2);
    }

    public final Message obtainMessage(int what, int arg1, int arg2, Object obj) {
        return Message.obtain(this, what, arg1, arg2, obj);
    }

    public final boolean post(Runnable r) {
        return sendMessageDelayed(Message.obtain(this, r), 0);
    }

    public final boolean postAtTime(Runnable r, long uptimeMillis) {
        return sendMessageAtTime(Message.obtain(this, r), uptimeMillis);
    }

    public final boolean postDelayed(Runnable r, long delayMillis) {
        return sendMessageDelayed(Message.obtain(this, r), delayMillis);
    }

    public final boolean postAtFrontOfQueue(Runnable r) {
        return sendMessageAtFrontOfQueue(Message.obtain(this, r));
    }

    public final void removeCallbacks(Runnable r) {
        queue.removeCallbacks(this, r, null);
    }

    public final void removeCallbacks(Runnable r, Object token) {
        queue.removeCallbacks(this, r, token);
    }

    public final boolean sendMessage(Message msg) {
        return sendMessageDelayed(msg, 0);
    }

    public final boolean sendEmptyMessage(int what) {
        return sendMessage(obtainMessage(what));
    }

    public final boolean sendEmptyMessageDelayed(int what, long delayMillis) {
        return sendMessageDelayed(obtainMessage(what), delayMillis);
    }

    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        if (delayMillis < 0) delayMillis = 0;
        return sendMessageAtTime(msg, SystemClock.uptimeMillis() + delayMillis);
    }

    public boolean sendMessageAtTime(Message msg, long uptimeMillis) {
        msg.target = this;
        return queue.enqueueMessage(msg, uptimeMillis);
    }

    public final boolean sendMessageAtFrontOfQueue(Message msg) {
        msg.target = this;
        return queue.enqueueMessage(msg, 0);
    }

    public final boolean hasMessages(int what) {
        return queue.hasMessages(this, what, null);
    }

    public final boolean hasCallbacks(Runnable r) {
        return queue.hasCallbacks(this, r);
    }

    public final void removeMessages(int what) {
        queue.removeMessages(this, what, null);
    }

    public final void removeMessages(int what, Object object) {
        queue.removeMessages(this, what, object);
    }

    public final void removeCallbacksAndMessages(Object token) {
        queue.removeCallbacksAndMessages(this, token);
    }

    @Override
    public String toString() {
        return "Handler (" + getClass().getName() + ") {" + Integer.toHexString(System.identityHashCode(this)) + "}";
    }
}
