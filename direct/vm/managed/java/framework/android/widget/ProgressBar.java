package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

public class ProgressBar extends View {
    private int mMax = 100;
    private int mProgress;
    private boolean mIndeterminate;

    public ProgressBar(Context context) {
        super(context);
    }

    public ProgressBar(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ProgressBar(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public synchronized void setIndeterminate(boolean indeterminate) {
        mIndeterminate = indeterminate;
    }

    public synchronized boolean isIndeterminate() {
        return mIndeterminate;
    }

    public synchronized void setMax(int max) {
        mMax = Math.max(max, 0);
        if (mProgress > mMax) mProgress = mMax;
        invalidate();
    }

    public synchronized int getMax() {
        return mMax;
    }

    public synchronized void setProgress(int progress) {
        mProgress = Math.max(0, Math.min(progress, mMax));
        invalidate();
    }

    public synchronized int getProgress() {
        return mIndeterminate ? 0 : mProgress;
    }
}
