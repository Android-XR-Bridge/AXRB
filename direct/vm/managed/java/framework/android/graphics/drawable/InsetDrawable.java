package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.TypedValue;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Insets another drawable: XML <inset>. */
public class InsetDrawable extends DrawableWrapper {
    private float mFracL, mFracT, mFracR, mFracB;
    private int mInsetL, mInsetT, mInsetR, mInsetB;
    private final Rect mTmp = new Rect();

    InsetDrawable() {
        super(null);
    }

    public InsetDrawable(Drawable drawable, int inset) {
        this(drawable, inset, inset, inset, inset);
    }

    public InsetDrawable(Drawable drawable, int insetLeft, int insetTop, int insetRight, int insetBottom) {
        super(drawable);
        mInsetL = insetLeft;
        mInsetT = insetTop;
        mInsetR = insetRight;
        mInsetB = insetBottom;
    }

    private int l(int w) { return mInsetL + (int) (mFracL * w); }
    private int t(int h) { return mInsetT + (int) (mFracT * h); }
    private int r(int w) { return mInsetR + (int) (mFracR * w); }
    private int b(int h) { return mInsetB + (int) (mFracB * h); }

    @Override
    protected void onBoundsChange(Rect bounds) {
        int w = bounds.width(), h = bounds.height();
        mTmp.set(bounds.left + l(w), bounds.top + t(h), bounds.right - r(w), bounds.bottom - b(h));
        super.onBoundsChange(mTmp);
    }

    @Override
    public boolean getPadding(Rect padding) {
        final boolean pad = super.getPadding(padding);
        Rect bounds = getBounds();
        int w = bounds.width(), h = bounds.height();
        padding.left += l(w);
        padding.right += r(w);
        padding.top += t(h);
        padding.bottom += b(h);
        return pad || (mInsetL | mInsetR | mInsetT | mInsetB) != 0;
    }

    @Override
    public int getIntrinsicWidth() {
        final int childWidth = getDrawable() != null ? getDrawable().getIntrinsicWidth() : -1;
        final float fraction = mFracL + mFracR;
        if (childWidth < 0 || fraction >= 1) return -1;
        return (int) (childWidth / (1 - fraction)) + mInsetL + mInsetR;
    }

    @Override
    public int getIntrinsicHeight() {
        final int childHeight = getDrawable() != null ? getDrawable().getIntrinsicHeight() : -1;
        final float fraction = mFracT + mFracB;
        if (childHeight < 0 || fraction >= 1) return -1;
        return (int) (childHeight / (1 - fraction)) + mInsetT + mInsetB;
    }

    private void readInset(TypedArray a, int index, int which) {
        TypedValue tv = a.peekValue(index);
        if (tv == null) return;
        if (tv.type == TypedValue.TYPE_FRACTION) {
            float f = tv.getFraction(1.0f, 1.0f);
            switch (which) {
                case 0: mFracL = f; mInsetL = 0; break;
                case 1: mFracT = f; mInsetT = 0; break;
                case 2: mFracR = f; mInsetR = 0; break;
                default: mFracB = f; mInsetB = 0; break;
            }
        } else {
            int px = a.getDimensionPixelOffset(index, 0);
            switch (which) {
                case 0: mInsetL = px; mFracL = 0; break;
                case 1: mInsetT = px; mFracT = 0; break;
                case 2: mInsetR = px; mFracR = 0; break;
                default: mInsetB = px; mFracB = 0; break;
            }
        }
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.InsetDrawable);
        readInset(a, android.R.styleable.InsetDrawable_inset, 0);
        readInset(a, android.R.styleable.InsetDrawable_inset, 1);
        readInset(a, android.R.styleable.InsetDrawable_inset, 2);
        readInset(a, android.R.styleable.InsetDrawable_inset, 3);
        readInset(a, android.R.styleable.InsetDrawable_insetLeft, 0);
        readInset(a, android.R.styleable.InsetDrawable_insetTop, 1);
        readInset(a, android.R.styleable.InsetDrawable_insetRight, 2);
        readInset(a, android.R.styleable.InsetDrawable_insetBottom, 3);
        inflateWrapped(r, parser, attrs, theme, a, android.R.styleable.InsetDrawable_drawable);
        a.recycle();
    }
}