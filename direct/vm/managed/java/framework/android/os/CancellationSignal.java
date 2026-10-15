package android.os;

public final class CancellationSignal {
    public interface OnCancelListener {
        void onCancel();
    }

    private boolean mCanceled;
    private OnCancelListener mListener;

    public CancellationSignal() {}

    public synchronized boolean isCanceled() {
        return mCanceled;
    }

    public void throwIfCanceled() {
        if (isCanceled()) throw new OperationCanceledException();
    }

    public void cancel() {
        OnCancelListener l;
        synchronized (this) {
            if (mCanceled) return;
            mCanceled = true;
            l = mListener;
        }
        if (l != null) l.onCancel();
    }

    public void setOnCancelListener(OnCancelListener listener) {
        boolean fire;
        synchronized (this) {
            mListener = listener;
            fire = mCanceled && listener != null;
        }
        if (fire) listener.onCancel();
    }
}
