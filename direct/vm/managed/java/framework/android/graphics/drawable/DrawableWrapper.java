package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.util.AttributeSet;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Wraps one drawable and forwards to it. */
public abstract class DrawableWrapper extends Drawable implements Drawable.Callback {
    private Drawable mDrawable;

    public DrawableWrapper(Drawable dr) {
        setDrawable(dr);
    }

    public void setDrawable(Drawable dr) {
        if (mDrawable != null) mDrawable.setCallback(null);
        mDrawable = dr;
        if (dr != null) {
            dr.setCallback(this);
            dr.setVisible(isVisible(), true);
            dr.setState(getState());
            dr.setLevel(getLevel());
            dr.setBounds(getBounds());
        }
        invalidateSelf();
    }

    public Drawable getDrawable() {
        return mDrawable;
    }

    /** Reads android:drawable or the child drawable element of `parser`'s tag. */
    void inflateWrapped(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme, TypedArray a,
            int drawableIndex) throws XmlPullParserException, IOException {
        Drawable dr = a.getDrawable(drawableIndex);
        if (dr == null) {
            final int innerDepth = parser.getDepth() + 1;
            int type;
            int depth;
            while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                    && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
                if (type == XmlPullParser.START_TAG && depth == innerDepth) {
                    dr = createFromXmlInner(r, parser, attrs, theme);
                }
            }
        }
        if (dr == null) {
            throw new XmlPullParserException(parser.getPositionDescription()
                    + ": <" + parser.getName() + "> requires a 'drawable' attribute or child tag defining a drawable");
        }
        setDrawable(dr);
    }

    @Override
    public void draw(Canvas canvas) {
        if (mDrawable != null) mDrawable.draw(canvas);
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        if (mDrawable != null) mDrawable.setBounds(bounds);
    }

    @Override
    public boolean getPadding(Rect padding) {
        return mDrawable != null && mDrawable.getPadding(padding);
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        final boolean superChanged = super.setVisible(visible, restart);
        final boolean changed = mDrawable != null && mDrawable.setVisible(visible, restart);
        return superChanged | changed;
    }

    @Override
    public void setAlpha(int alpha) {
        if (mDrawable != null) mDrawable.setAlpha(alpha);
    }

    @Override
    public int getAlpha() {
        return mDrawable != null ? mDrawable.getAlpha() : 255;
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        if (mDrawable != null) mDrawable.setColorFilter(colorFilter);
    }

    @Override
    public void setTintList(ColorStateList tint) {
        if (mDrawable != null) mDrawable.setTintList(tint);
    }

    @Override
    public int getOpacity() {
        return mDrawable != null ? mDrawable.getOpacity() : PixelFormat.TRANSPARENT;
    }

    @Override
    public boolean isStateful() {
        return mDrawable != null && mDrawable.isStateful();
    }

    @Override
    protected boolean onStateChange(int[] state) {
        if (mDrawable != null && mDrawable.isStateful()) {
            final boolean changed = mDrawable.setState(state);
            if (changed) onBoundsChange(getBounds());
            return changed;
        }
        return false;
    }

    @Override
    protected boolean onLevelChange(int level) {
        return mDrawable != null && mDrawable.setLevel(level);
    }

    @Override
    public int getIntrinsicWidth() {
        return mDrawable != null ? mDrawable.getIntrinsicWidth() : -1;
    }

    @Override
    public int getIntrinsicHeight() {
        return mDrawable != null ? mDrawable.getIntrinsicHeight() : -1;
    }

    @Override
    public void jumpToCurrentState() {
        if (mDrawable != null) mDrawable.jumpToCurrentState();
    }

    @Override
    public void invalidateDrawable(Drawable who) {
        invalidateSelf();
    }

    @Override
    public void scheduleDrawable(Drawable who, Runnable what, long when) {
        scheduleSelf(what, when);
    }

    @Override
    public void unscheduleDrawable(Drawable who, Runnable what) {
        unscheduleSelf(what);
    }
}