package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.TypedValue;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Rotates another drawable by its level: XML <rotate>. As <animated-rotate>
 * it spins continuously while visible (indeterminate progress).
 */
public class RotateDrawable extends DrawableWrapper implements Animatable {
    private static final float MAX_LEVEL = 10000.0f;
    private float mFromDegrees = 0f, mToDegrees = 360f;
    private float mPivotX = 0.5f, mPivotY = 0.5f;
    private boolean mPivotXRel = true, mPivotYRel = true;
    private float mCurrentDegrees;
    private boolean mAnimated;
    private boolean mRunning;
    private final Runnable mTick = new Runnable() {
        public void run() {
            mCurrentDegrees = (mCurrentDegrees + 30f) % 360f;
            invalidateSelf();
            if (mRunning) scheduleSelf(this, SystemClock.uptimeMillis() + 83);
        }
    };

    public RotateDrawable() {
        super(null);
    }

    public void setFromDegrees(float fromDegrees) {
        mFromDegrees = fromDegrees;
    }

    public void setToDegrees(float toDegrees) {
        mToDegrees = toDegrees;
    }

    public void setPivotX(float pivotX) {
        mPivotX = pivotX;
    }

    public void setPivotY(float pivotY) {
        mPivotY = pivotY;
    }

    @Override
    public void draw(Canvas canvas) {
        final Drawable d = getDrawable();
        if (d == null) return;
        final Rect bounds = d.getBounds();
        final int w = bounds.right - bounds.left;
        final int h = bounds.bottom - bounds.top;
        final float px = mPivotXRel ? (w * mPivotX) : mPivotX;
        final float py = mPivotYRel ? (h * mPivotY) : mPivotY;
        final int saveCount = canvas.save();
        canvas.rotate(mCurrentDegrees, px + bounds.left, py + bounds.top);
        d.draw(canvas);
        canvas.restoreToCount(saveCount);
    }

    @Override
    protected boolean onLevelChange(int level) {
        super.onLevelChange(level);
        if (!mAnimated) {
            final float value = level / MAX_LEVEL;
            mCurrentDegrees = mFromDegrees + (mToDegrees - mFromDegrees) * value;
            invalidateSelf();
        }
        return true;
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        final boolean changed = super.setVisible(visible, restart);
        if (mAnimated) {
            if (visible) start(); else stop();
        }
        return changed;
    }

    @Override
    public void start() {
        if (!mRunning) {
            mRunning = true;
            scheduleSelf(mTick, SystemClock.uptimeMillis() + 83);
        }
    }

    @Override
    public void stop() {
        mRunning = false;
        unscheduleSelf(mTick);
    }

    @Override
    public boolean isRunning() {
        return mRunning;
    }

    private void readPivot(TypedArray a, int index, boolean x) {
        TypedValue tv = a.peekValue(index);
        if (tv == null) return;
        boolean rel = tv.type == TypedValue.TYPE_FRACTION;
        float value = rel ? tv.getFraction(1.0f, 1.0f) : tv.getFloat();
        if (x) {
            mPivotXRel = rel;
            mPivotX = value;
        } else {
            mPivotYRel = rel;
            mPivotY = value;
        }
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        mAnimated = "animated-rotate".equals(parser.getName());
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.RotateDrawable);
        mFromDegrees = a.getFloat(android.R.styleable.RotateDrawable_fromDegrees, mFromDegrees);
        mToDegrees = a.getFloat(android.R.styleable.RotateDrawable_toDegrees, mToDegrees);
        readPivot(a, android.R.styleable.RotateDrawable_pivotX, true);
        readPivot(a, android.R.styleable.RotateDrawable_pivotY, false);
        mCurrentDegrees = mFromDegrees;
        inflateWrapped(r, parser, attrs, theme, a, android.R.styleable.RotateDrawable_drawable);
        a.recycle();
    }
}