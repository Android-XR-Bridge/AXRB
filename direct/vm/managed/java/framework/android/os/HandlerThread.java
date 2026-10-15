package android.os;

public class HandlerThread extends Thread {
    private int mPriority;
    private Looper mLooper;
    private Handler mHandler;

    public HandlerThread(String name) {
        super(name);
        mPriority = 0;
    }

    public HandlerThread(String name, int priority) {
        super(name);
        mPriority = priority;
    }

    protected void onLooperPrepared() {}

    @Override
    public void run() {
        Looper.prepare();
        synchronized (this) {
            mLooper = Looper.myLooper();
            notifyAll();
        }
        onLooperPrepared();
        Looper.loop();
    }

    public Looper getLooper() {
        if (!isAlive()) {
            synchronized (this) {
                if (mLooper == null) return null;
            }
        }
        boolean interrupted = false;
        synchronized (this) {
            while (isAlive() && mLooper == null) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    interrupted = true;
                }
            }
        }
        if (interrupted) Thread.currentThread().interrupt();
        return mLooper;
    }

    public Handler getThreadHandler() {
        if (mHandler == null) mHandler = new Handler(getLooper());
        return mHandler;
    }

    public boolean quit() {
        Looper looper = getLooper();
        if (looper != null) {
            looper.quit();
            return true;
        }
        return false;
    }

    public boolean quitSafely() {
        Looper looper = getLooper();
        if (looper != null) {
            looper.quitSafely();
            return true;
        }
        return false;
    }

    public int getThreadId() {
        return (int) getId();
    }
}
