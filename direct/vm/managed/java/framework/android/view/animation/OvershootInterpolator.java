package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

public class OvershootInterpolator extends BaseInterpolator {
    private final float mTension;

    public OvershootInterpolator() {
        mTension = 2.0f;
    }

    public OvershootInterpolator(float tension) {
        mTension = tension;
    }

    public OvershootInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.OvershootInterpolator);
        mTension = a.getFloat(android.R.styleable.OvershootInterpolator_tension, 2.0f);
        a.recycle();
    }

    public float getInterpolation(float t) {
        t -= 1.0f;
        return t * t * ((mTension + 1) * t + mTension) + 1.0f;
    }
}
