package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

public class AnticipateInterpolator extends BaseInterpolator {
    private final float mTension;

    public AnticipateInterpolator() {
        mTension = 2.0f;
    }

    public AnticipateInterpolator(float tension) {
        mTension = tension;
    }

    public AnticipateInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.AnticipateInterpolator);
        mTension = a.getFloat(android.R.styleable.AnticipateInterpolator_tension, 2.0f);
        a.recycle();
    }

    public float getInterpolation(float t) {
        return t * t * ((mTension + 1) * t - mTension);
    }
}
