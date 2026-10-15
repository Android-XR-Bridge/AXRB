package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

public class DecelerateInterpolator extends BaseInterpolator {
    private float mFactor = 1.0f;

    public DecelerateInterpolator() {}

    public DecelerateInterpolator(float factor) {
        mFactor = factor;
    }

    public DecelerateInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.DecelerateInterpolator);
        mFactor = a.getFloat(android.R.styleable.DecelerateInterpolator_factor, 1.0f);
        a.recycle();
    }

    public float getInterpolation(float input) {
        if (mFactor == 1.0f) return 1.0f - (1.0f - input) * (1.0f - input);
        return (float) (1.0f - Math.pow((1.0f - input), 2 * mFactor));
    }
}
