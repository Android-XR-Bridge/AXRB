package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.AttributeSet;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Touch feedback: the layers, plus the ripple color over them while pressed
 * (a static highlight -- the expanding animation is not rendered).
 */
public class RippleDrawable extends LayerDrawable {
    public static final int RADIUS_AUTO = -1;

    private ColorStateList mColor = ColorStateList.valueOf(0x1f000000);
    private int mMaxRadius = RADIUS_AUTO;
    private boolean mActive;
    private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    RippleDrawable() {}

    public RippleDrawable(ColorStateList color, Drawable content, Drawable mask) {
        if (content != null) addLayer(content);
        if (mask != null) {
            int i = addLayer(mask);
            setId(i, android.R.id.mask);
        }
        if (color != null) mColor = color;
    }

    public void setColor(ColorStateList color) {
        mColor = color;
        invalidateSelf();
    }

    public void setRadius(int radius) {
        mMaxRadius = radius;
    }

    public int getRadius() {
        return mMaxRadius;
    }

    @Override
    public boolean isStateful() {
        return true;
    }

    @Override
    protected boolean onStateChange(int[] state) {
        boolean changed = super.onStateChange(state);
        boolean pressed = false, enabled = false, focused = false, hovered = false;
        for (int s : state) {
            if (s == android.R.attr.state_pressed) pressed = true;
            else if (s == android.R.attr.state_enabled) enabled = true;
            else if (s == android.R.attr.state_focused) focused = true;
            else if (s == android.R.attr.state_hovered) hovered = true;
        }
        boolean active = enabled && (pressed || focused || hovered);
        if (active != mActive) {
            mActive = active;
            changed = true;
        }
        return changed;
    }

    @Override
    public void draw(Canvas canvas) {
        for (Layer l : mLayers) {
            if (l.drawable != null && l.id != android.R.id.mask) l.drawable.draw(canvas);
        }
        if (!mActive) return;
        int color = mColor.getColorForState(getState(), mColor.getDefaultColor());
        mPaint.setColor(color);
        Rect b = getBounds();
        Drawable mask = findDrawableByLayerId(android.R.id.mask);
        if (mask != null || getNumberOfLayers() > 0) {
            canvas.drawRect(b, mPaint);
        } else {
            float radius = mMaxRadius > 0 ? mMaxRadius : Math.max(b.width(), b.height()) / 2f;
            canvas.drawCircle(b.exactCenterX(), b.exactCenterY(), radius, mPaint);
        }
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.RippleDrawable);
        try {
            ColorStateList color = a.getColorStateList(android.R.styleable.RippleDrawable_color);
            if (color != null) mColor = color;
        } catch (UnsupportedOperationException e) {
            // ?attr without a theme: keep the default highlight
        }
        mMaxRadius = a.getDimensionPixelSize(android.R.styleable.RippleDrawable_radius, mMaxRadius);
        a.recycle();
        inflateLayers(r, parser, attrs, theme);
        updateLayerBounds(getBounds());
    }
}