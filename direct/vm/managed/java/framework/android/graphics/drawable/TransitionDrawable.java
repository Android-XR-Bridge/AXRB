package android.graphics.drawable;

import android.graphics.Canvas;
import android.os.SystemClock;

/** Cross-fades from its first layer to its second: XML <transition>. */
public class TransitionDrawable extends LayerDrawable {
    private long mStart = -1;
    private int mDuration;
    private boolean mReverse;
    private boolean mCrossFade;

    public TransitionDrawable(Drawable[] layers) {
        super(layers);
    }

    TransitionDrawable() {}

    public void startTransition(int durationMillis) {
        mStart = SystemClock.uptimeMillis();
        mDuration = Math.max(1, durationMillis);
        mReverse = false;
        invalidateSelf();
    }

    public void reverseTransition(int duration) {
        mStart = SystemClock.uptimeMillis();
        mDuration = Math.max(1, duration);
        mReverse = true;
        invalidateSelf();
    }

    public void resetTransition() {
        mStart = -1;
        mReverse = false;
        invalidateSelf();
    }

    public void setCrossFadeEnabled(boolean enabled) {
        mCrossFade = enabled;
    }

    public boolean isCrossFadeEnabled() {
        return mCrossFade;
    }

    @Override
    public void draw(Canvas canvas) {
        if (getNumberOfLayers() < 2) {
            super.draw(canvas);
            return;
        }
        float t = 0;
        boolean done = true;
        if (mStart >= 0) {
            t = Math.min(1f, (SystemClock.uptimeMillis() - mStart) / (float) mDuration);
            done = t >= 1f;
        }
        if (mReverse) t = 1f - t;
        int alpha = (int) (t * 255);
        Drawable first = getDrawable(0);
        Drawable second = getDrawable(1);
        if (!mCrossFade || alpha < 255) {
            first.setAlpha(mCrossFade ? 255 - alpha : 255);
            first.draw(canvas);
            first.setAlpha(255);
        }
        if (alpha > 0) {
            second.setAlpha(alpha);
            second.draw(canvas);
            second.setAlpha(255);
        }
        if (!done) invalidateSelf();
    }
}