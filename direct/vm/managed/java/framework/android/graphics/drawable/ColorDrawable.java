package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.util.AttributeSet;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Fills its bounds with one color. */
public class ColorDrawable extends Drawable {
    private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int mBaseColor;
    private int mUseColor;
    private ColorFilter mColorFilter;

    public ColorDrawable() {}

    public ColorDrawable(int color) {
        setColor(color);
    }

    public int getColor() {
        return mUseColor;
    }

    public void setColor(int color) {
        if (mBaseColor != color || mUseColor != color) {
            mBaseColor = mUseColor = color;
            invalidateSelf();
        }
    }

    @Override
    public int getAlpha() {
        return mUseColor >>> 24;
    }

    @Override
    public void setAlpha(int alpha) {
        alpha += alpha >> 7;
        final int baseAlpha = mBaseColor >>> 24;
        final int useAlpha = baseAlpha * alpha >> 8;
        final int useColor = (mBaseColor << 8 >>> 8) | (useAlpha << 24);
        if (mUseColor != useColor) {
            mUseColor = useColor;
            invalidateSelf();
        }
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
        invalidateSelf();
    }

    @Override
    public ColorFilter getColorFilter() {
        return mColorFilter;
    }

    @Override
    public void draw(Canvas canvas) {
        ColorFilter filter = mColorFilter != null ? mColorFilter : mTintFilter;
        if ((mUseColor >>> 24) != 0 || filter != null) {
            mPaint.setColorFilter(filter);
            mPaint.setColor(mUseColor);
            canvas.drawRect(getBounds(), mPaint);
        }
    }

    @Override
    public int getOpacity() {
        if (mColorFilter != null || mTintFilter != null) return PixelFormat.TRANSLUCENT;
        switch (mUseColor >>> 24) {
            case 255: return PixelFormat.OPAQUE;
            case 0: return PixelFormat.TRANSPARENT;
        }
        return PixelFormat.TRANSLUCENT;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.ColorDrawable);
        setColor(a.getColor(android.R.styleable.ColorDrawable_color, mBaseColor));
        a.recycle();
    }

    @Override
    public ConstantState getConstantState() {
        final int color = mBaseColor;
        return new ConstantState() {
            @Override
            public Drawable newDrawable() {
                return new ColorDrawable(color);
            }

            @Override
            public int getChangingConfigurations() {
                return 0;
            }
        };
    }
}