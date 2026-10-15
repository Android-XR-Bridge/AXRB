package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

/** Animates a view's rotation about a pivot. */
public class RotateAnimation extends Animation {
    private float mFromDegrees;
    private float mToDegrees;
    private int mPivotXType = ABSOLUTE;
    private int mPivotYType = ABSOLUTE;
    private float mPivotXValue = 0.0f;
    private float mPivotYValue = 0.0f;
    private float mPivotX;
    private float mPivotY;

    public RotateAnimation(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.RotateAnimation);
        mFromDegrees = a.getFloat(android.R.styleable.RotateAnimation_fromDegrees, 0.0f);
        mToDegrees = a.getFloat(android.R.styleable.RotateAnimation_toDegrees, 0.0f);
        Description d = Description.parseValue(a.peekValue(android.R.styleable.RotateAnimation_pivotX));
        mPivotXType = d.type;
        mPivotXValue = d.value;
        d = Description.parseValue(a.peekValue(android.R.styleable.RotateAnimation_pivotY));
        mPivotYType = d.type;
        mPivotYValue = d.value;
        a.recycle();
        initializePivotPoint();
    }

    public RotateAnimation(float fromDegrees, float toDegrees) {
        mFromDegrees = fromDegrees;
        mToDegrees = toDegrees;
    }

    public RotateAnimation(float fromDegrees, float toDegrees, float pivotX, float pivotY) {
        mFromDegrees = fromDegrees;
        mToDegrees = toDegrees;
        mPivotXValue = pivotX;
        mPivotYValue = pivotY;
        initializePivotPoint();
    }

    public RotateAnimation(float fromDegrees, float toDegrees, int pivotXType, float pivotXValue, int pivotYType,
            float pivotYValue) {
        mFromDegrees = fromDegrees;
        mToDegrees = toDegrees;
        mPivotXValue = pivotXValue;
        mPivotXType = pivotXType;
        mPivotYValue = pivotYValue;
        mPivotYType = pivotYType;
        initializePivotPoint();
    }

    private void initializePivotPoint() {
        if (mPivotXType == ABSOLUTE) mPivotX = mPivotXValue;
        if (mPivotYType == ABSOLUTE) mPivotY = mPivotYValue;
    }

    @Override
    protected void applyTransformation(float interpolatedTime, Transformation t) {
        float degrees = mFromDegrees + ((mToDegrees - mFromDegrees) * interpolatedTime);
        float scale = getScaleFactor();
        if (mPivotX == 0.0f && mPivotY == 0.0f) {
            t.getMatrix().setRotate(degrees);
        } else {
            t.getMatrix().setRotate(degrees, mPivotX * scale, mPivotY * scale);
        }
    }

    @Override
    public void initialize(int width, int height, int parentWidth, int parentHeight) {
        super.initialize(width, height, parentWidth, parentHeight);
        mPivotX = resolveSize(mPivotXType, mPivotXValue, width, parentWidth);
        mPivotY = resolveSize(mPivotYType, mPivotYValue, height, parentHeight);
    }
}
