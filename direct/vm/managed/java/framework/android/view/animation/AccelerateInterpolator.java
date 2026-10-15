package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

public class AccelerateInterpolator extends BaseInterpolator {
    private final float mFactor;
    private final double mDoubleFactor;

    public AccelerateInterpolator() {
        this(1.0f);
    }

    public AccelerateInterpolator(float factor) {
        mFactor = factor;
        mDoubleFactor = 2 * mFactor;
    }

    public AccelerateInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.AccelerateInterpolator);
        mFactor = a.getFloat(android.R.styleable.AccelerateInterpolator_factor, 1.0f);
        mDoubleFactor = 2 * mFactor;
        a.recycle();
    }

    public float getInterpolation(float input) {
        if (mFactor == 1.0f) return input * input;
        return (float) Math.pow(input, mDoubleFactor);
    }
}
