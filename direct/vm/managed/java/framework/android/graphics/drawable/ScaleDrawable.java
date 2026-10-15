package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Scales another drawable by its level: XML <scale>. */
public class ScaleDrawable extends DrawableWrapper {
    private static final int MAX_LEVEL = 10000;
    private float mScaleWidth = -1f, mScaleHeight = -1f;
    private int mGravity = Gravity.LEFT;
    private boolean mUseIntrinsicSizeAsMin;
    private final Rect mTmp = new Rect();

    ScaleDrawable() {
        super(null);
    }

    public ScaleDrawable(Drawable drawable, int gravity, float scaleWidth, float scaleHeight) {
        super(drawable);
        mGravity = gravity;
        mScaleWidth = scaleWidth;
        mScaleHeight = scaleHeight;
    }

    @Override
    public void draw(Canvas canvas) {
        final Drawable d = getDrawable();
        if (d != null && d.getLevel() != 0) d.draw(canvas);
    }

    @Override
    protected boolean onLevelChange(int level) {
        super.onLevelChange(level);
        onBoundsChange(getBounds());
        invalidateSelf();
        return true;
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        final Drawable d = getDrawable();
        if (d == null) return;
        final Rect r = mTmp;
        final int level = getLevel();
        int w = bounds.width();
        if (mScaleWidth > 0) {
            final int iw = mUseIntrinsicSizeAsMin ? d.getIntrinsicWidth() : 0;
            w -= (int) ((w - iw) * (MAX_LEVEL - level) * mScaleWidth / MAX_LEVEL);
        }
        int h = bounds.height();
        if (mScaleHeight > 0) {
            final int ih = mUseIntrinsicSizeAsMin ? d.getIntrinsicHeight() : 0;
            h -= (int) ((h - ih) * (MAX_LEVEL - level) * mScaleHeight / MAX_LEVEL);
        }
        Gravity.apply(mGravity, w, h, bounds, r, getLayoutDirection());
        if (w > 0 && h > 0) d.setBounds(r.left, r.top, r.right, r.bottom);
    }

    private static float getPercent(TypedArray a, int index, float defaultValue) {
        TypedValue tv = a.peekValue(index);
        if (tv == null) return defaultValue;
        if (tv.type == TypedValue.TYPE_FRACTION) return tv.getFraction(1.0f, 1.0f);
        CharSequence s = tv.coerceToString();
        if (s != null && s.toString().endsWith("%")) {
            try {
                return Float.parseFloat(s.toString().substring(0, s.length() - 1)) / 100.0f;
            } catch (NumberFormatException e) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.ScaleDrawable);
        mScaleWidth = getPercent(a, android.R.styleable.ScaleDrawable_scaleWidth, mScaleWidth);
        mScaleHeight = getPercent(a, android.R.styleable.ScaleDrawable_scaleHeight, mScaleHeight);
        mGravity = a.getInt(android.R.styleable.ScaleDrawable_scaleGravity, mGravity);
        mUseIntrinsicSizeAsMin = a.getBoolean(android.R.styleable.ScaleDrawable_useIntrinsicSizeAsMinimum, false);
        inflateWrapped(r, parser, attrs, theme, a, android.R.styleable.ScaleDrawable_drawable);
        a.recycle();
    }
}