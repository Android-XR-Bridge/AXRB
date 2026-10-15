package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

/** A quadratic or cubic Bezier curve from (0,0) to (1,1), evaluated by bisection on x. */
public class PathInterpolator extends BaseInterpolator {
    private final float mX1;
    private final float mY1;
    private final float mX2;
    private final float mY2;
    private final boolean mQuadratic;

    public PathInterpolator(float controlX, float controlY) {
        mX1 = controlX;
        mY1 = controlY;
        mX2 = controlX;
        mY2 = controlY;
        mQuadratic = true;
    }

    public PathInterpolator(float controlX1, float controlY1, float controlX2, float controlY2) {
        mX1 = controlX1;
        mY1 = controlY1;
        mX2 = controlX2;
        mY2 = controlY2;
        mQuadratic = false;
    }

    public PathInterpolator(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.PathInterpolator);
        mX1 = a.getFloat(android.R.styleable.PathInterpolator_controlX1, 0);
        mY1 = a.getFloat(android.R.styleable.PathInterpolator_controlY1, 0);
        boolean cubic = a.hasValue(android.R.styleable.PathInterpolator_controlX2);
        mX2 = cubic ? a.getFloat(android.R.styleable.PathInterpolator_controlX2, 0) : mX1;
        mY2 = cubic ? a.getFloat(android.R.styleable.PathInterpolator_controlY2, 0) : mY1;
        mQuadratic = !cubic;
        a.recycle();
    }

    private float curve(float t, float p1, float p2) {
        float u = 1 - t;
        if (mQuadratic) return 2 * u * t * p1 + t * t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    }

    public float getInterpolation(float t) {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        float lo = 0;
        float hi = 1;
        float mid = t;
        for (int i = 0; i < 32; i++) {
            mid = (lo + hi) / 2;
            float x = curve(mid, mX1, mX2);
            if (Math.abs(x - t) < 1e-5f) break;
            if (x < t) lo = mid; else hi = mid;
        }
        return curve(mid, mY1, mY2);
    }
}
