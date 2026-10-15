package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

public class CycleInterpolator extends BaseInterpolator {
    private final float mCycles;

    public CycleInterpolator(float cycles) {
        mCycles = cycles;
    }

    public CycleInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.CycleInterpolator);
        mCycles = a.getFloat(android.R.styleable.CycleInterpolator_cycles, 1.0f);
        a.recycle();
    }

    public float getInterpolation(float input) {
        return (float) (Math.sin(2 * mCycles * Math.PI * input));
    }
}
