package android.view;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.util.ArrayList;

/**
 * Coordinates input, animation and traversal work into frames. The desktop
 * host has no vsync signal; frames are paced at 60 Hz on the looper's clock
 * (uptimeMillis), and only while callbacks are pending.
 */
public final class Choreographer {
    public static final int CALLBACK_INPUT = 0;
    public static final int CALLBACK_ANIMATION = 1;
    public static final int CALLBACK_INSETS_ANIMATION = 2;
    public static final int CALLBACK_TRAVERSAL = 3;
    public static final int CALLBACK_COMMIT = 4;
    private static final int CALLBACK_LAST = CALLBACK_COMMIT;

    private static final long FRAME_INTERVAL_NANOS = 16666667L;
    private static final Object FRAME_CALLBACK_TOKEN = new Object();

    private static final ThreadLocal<Choreographer> sThreadInstance = new ThreadLocal<Choreographer>() {
        @Override
        protected Choreographer initialValue() {
            Looper looper = Looper.myLooper();
            if (looper == null) throw new IllegalStateException("The current thread must have a looper!");
            return new Choreographer(looper);
        }
    };

    public interface FrameCallback {
        void doFrame(long frameTimeNanos);
    }

    private static final class CallbackRecord {
        long dueTime;
        Object action; // Runnable or FrameCallback
        Object token;

        void run(long frameTimeNanos) {
            if (token == FRAME_CALLBACK_TOKEN) {
                ((FrameCallback) action).doFrame(frameTimeNanos);
            } else {
                ((Runnable) action).run();
            }
        }
    }

    private final Looper mLooper;
    private final Handler mHandler;
    private final ArrayList<CallbackRecord>[] mCallbackQueues;
    private boolean mFrameScheduled;
    private long mLastFrameTimeNanos;
    private long mFrameTimeNanos;
    private boolean mInFrame;

    private final Runnable mFrameRunnable = new Runnable() {
        public void run() {
            doFrame();
        }
    };

    private final Runnable mScheduleRunnable = new Runnable() {
        public void run() {
            scheduleFrame();
        }
    };

    @SuppressWarnings("unchecked")
    private Choreographer(Looper looper) {
        mLooper = looper;
        mHandler = new Handler(looper);
        mCallbackQueues = new ArrayList[CALLBACK_LAST + 1];
        for (int i = 0; i <= CALLBACK_LAST; i++) mCallbackQueues[i] = new ArrayList<CallbackRecord>();
    }

    public static Choreographer getInstance() {
        return sThreadInstance.get();
    }

    public static Choreographer getMainThreadInstance() {
        return getInstance();
    }

    @Deprecated
    public static long getFrameDelay() {
        return 10;
    }

    @Deprecated
    public static void setFrameDelay(long frameDelay) {}

    public long getFrameTime() {
        return getFrameTimeNanos() / 1000000L;
    }

    public long getFrameTimeNanos() {
        if (!mInFrame) throw new IllegalStateException("This method must only be called as part of a callback while a frame is in progress.");
        return mFrameTimeNanos;
    }

    public long getLastFrameTimeNanos() {
        return mLastFrameTimeNanos;
    }

    public long getFrameIntervalNanos() {
        return FRAME_INTERVAL_NANOS;
    }

    // --- callbacks (the typed form is hidden API on Android) ----------------------------------

    public void postCallback(int callbackType, Runnable action, Object token) {
        postCallbackDelayed(callbackType, action, token, 0);
    }

    public void postCallbackDelayed(int callbackType, Runnable action, Object token, long delayMillis) {
        if (action == null) throw new IllegalArgumentException("action must not be null");
        if (callbackType < 0 || callbackType > CALLBACK_LAST) throw new IllegalArgumentException("callbackType is invalid");
        postCallbackDelayedInternal(callbackType, action, token, delayMillis);
    }

    public void removeCallbacks(int callbackType, Runnable action, Object token) {
        if (callbackType < 0 || callbackType > CALLBACK_LAST) throw new IllegalArgumentException("callbackType is invalid");
        removeCallbacksInternal(callbackType, action, token);
    }

    public void postFrameCallback(FrameCallback callback) {
        postFrameCallbackDelayed(callback, 0);
    }

    public void postFrameCallbackDelayed(FrameCallback callback, long delayMillis) {
        if (callback == null) throw new IllegalArgumentException("callback must not be null");
        postCallbackDelayedInternal(CALLBACK_ANIMATION, callback, FRAME_CALLBACK_TOKEN, delayMillis);
    }

    public void removeFrameCallback(FrameCallback callback) {
        if (callback == null) throw new IllegalArgumentException("callback must not be null");
        removeCallbacksInternal(CALLBACK_ANIMATION, callback, FRAME_CALLBACK_TOKEN);
    }

    private void postCallbackDelayedInternal(int callbackType, Object action, Object token, long delayMillis) {
        final long now = SystemClock.uptimeMillis();
        final long dueTime = now + Math.max(0, delayMillis);
        CallbackRecord r = new CallbackRecord();
        r.dueTime = dueTime;
        r.action = action;
        r.token = token;
        mCallbackQueues[callbackType].add(r);
        if (dueTime <= now) {
            scheduleFrame();
        } else {
            mHandler.postAtTime(mScheduleRunnable, dueTime);
        }
    }

    private void removeCallbacksInternal(int callbackType, Object action, Object token) {
        ArrayList<CallbackRecord> q = mCallbackQueues[callbackType];
        for (int i = q.size() - 1; i >= 0; i--) {
            CallbackRecord r = q.get(i);
            if ((action == null || r.action == action) && (token == null || r.token == token)) q.remove(i);
        }
    }

    private void scheduleFrame() {
        if (mFrameScheduled) return;
        mFrameScheduled = true;
        final long nowMs = SystemClock.uptimeMillis();
        final long nextFrameMs = (mLastFrameTimeNanos + FRAME_INTERVAL_NANOS) / 1000000L;
        mHandler.postAtTime(mFrameRunnable, Math.max(nowMs, nextFrameMs));
    }

    void doFrame() {
        mFrameScheduled = false;
        final long nowMs = SystemClock.uptimeMillis();
        mFrameTimeNanos = nowMs * 1000000L;
        mLastFrameTimeNanos = mFrameTimeNanos;
        mInFrame = true;
        try {
            for (int type = 0; type <= CALLBACK_LAST; type++) doCallbacks(type, nowMs);
        } finally {
            mInFrame = false;
        }
    }

    private void doCallbacks(int callbackType, long nowMs) {
        ArrayList<CallbackRecord> q = mCallbackQueues[callbackType];
        if (q.isEmpty()) return;
        ArrayList<CallbackRecord> due = null;
        for (int i = 0; i < q.size(); ) {
            CallbackRecord r = q.get(i);
            if (r.dueTime <= nowMs) {
                if (due == null) due = new ArrayList<CallbackRecord>();
                due.add(r);
                q.remove(i);
            } else {
                i++;
            }
        }
        if (due == null) return;
        for (int i = 0; i < due.size(); i++) due.get(i).run(mFrameTimeNanos);
    }

    @Override
    public String toString() {
        return "Choreographer{" + mLooper + "}";
    }
}
