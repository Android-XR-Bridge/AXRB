package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.Gravity;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Shows a level-proportional part of another drawable (progress bars): XML <clip>. */
public class ClipDrawable extends DrawableWrapper {
    public static final int HORIZONTAL = 1;
    public static final int VERTICAL = 2;
    private static final int MAX_LEVEL = 10000;

    private int mGravity = Gravity.LEFT;
    private int mOrientation = HORIZONTAL;
    private final Rect mTmp = new Rect();

    ClipDrawable() {
        super(null);
    }

    public ClipDrawable(Drawable drawable, int gravity, int orientation) {
        super(drawable);
        mGravity = gravity;
        mOrientation = orientation;
    }

    @Override
    protected boolean onLevelChange(int level) {
        super.onLevelChange(level);
        invalidateSelf();
        return true;
    }

    @Override
    public void draw(Canvas canvas) {
        final Drawable dr = getDrawable();
        if (dr == null || dr.getLevel() == 0) return;
        final Rect bounds = getBounds();
        final int level = getLevel();
        int w = bounds.width();
        if ((mOrientation & HORIZONTAL) != 0) w -= (w - 0) * (MAX_LEVEL - level) / MAX_LEVEL;
        int h = bounds.height();
        if ((mOrientation & VERTICAL) != 0) h -= (h - 0) * (MAX_LEVEL - level) / MAX_LEVEL;
        Gravity.apply(mGravity, w, h, bounds, mTmp, getLayoutDirection());
        if (w > 0 && h > 0) {
            canvas.save();
            canvas.clipRect(mTmp);
            dr.draw(canvas);
            canvas.restore();
        }
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.ClipDrawable);
        mOrientation = a.getInt(android.R.styleable.ClipDrawable_clipOrientation, mOrientation);
        mGravity = a.getInt(android.R.styleable.ClipDrawable_gravity, mGravity);
        inflateWrapped(r, parser, attrs, theme, a, android.R.styleable.ClipDrawable_drawable);
        a.recycle();
    }
}