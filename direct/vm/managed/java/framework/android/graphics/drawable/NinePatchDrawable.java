package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.NinePatch;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** A stretchable bitmap (compiled .9.png). */
public class NinePatchDrawable extends Drawable {
    private NinePatch mNinePatch;
    private final Rect mPadding = new Rect();
    private final Paint mPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private ColorFilter mColorFilter;
    private int mTargetDensity = DisplayMetrics.DENSITY_DEVICE_STABLE;

    NinePatchDrawable() {}

    public NinePatchDrawable(Resources res, Bitmap bitmap, byte[] chunk, Rect padding, String srcName) {
        mNinePatch = new NinePatch(bitmap, chunk, srcName);
        if (padding != null) mPadding.set(padding);
        if (res != null) mTargetDensity = res.getDisplayMetrics().densityDpi;
    }

    public NinePatchDrawable(Resources res, NinePatch patch) {
        mNinePatch = patch;
        patch.getPadding(mPadding);
        if (res != null) mTargetDensity = res.getDisplayMetrics().densityDpi;
    }

    @Override
    public void draw(Canvas canvas) {
        if (mNinePatch == null) return;
        mPaint.setColorFilter(mColorFilter != null ? mColorFilter : mTintFilter);
        mNinePatch.draw(canvas, getBounds(), mPaint);
    }

    @Override
    public boolean getPadding(Rect padding) {
        padding.set(mPadding);
        return (padding.left | padding.top | padding.right | padding.bottom) != 0;
    }

    @Override
    public int getIntrinsicWidth() {
        return mNinePatch != null ? mNinePatch.getBitmap().getScaledWidth(mTargetDensity) : -1;
    }

    @Override
    public int getIntrinsicHeight() {
        return mNinePatch != null ? mNinePatch.getBitmap().getScaledHeight(mTargetDensity) : -1;
    }

    @Override
    public void setAlpha(int alpha) {
        mPaint.setAlpha(alpha);
        invalidateSelf();
    }

    @Override
    public int getAlpha() {
        return mPaint.getAlpha();
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
        invalidateSelf();
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.NinePatchDrawable);
        final int srcResId = a.getResourceId(android.R.styleable.NinePatchDrawable_src, 0);
        a.recycle();
        Drawable src = srcResId != 0 ? r.getDrawable(srcResId, theme) : null;
        if (!(src instanceof NinePatchDrawable)) {
            throw new XmlPullParserException(parser.getPositionDescription()
                    + ": <nine-patch> requires a valid 9-patch source image");
        }
        NinePatchDrawable np = (NinePatchDrawable) src;
        mNinePatch = np.mNinePatch;
        mPadding.set(np.mPadding);
        mTargetDensity = np.mTargetDensity;
    }

    @Override
    public ConstantState getConstantState() {
        final NinePatch patch = mNinePatch;
        final Rect padding = new Rect(mPadding);
        final int density = mTargetDensity;
        return new ConstantState() {
            @Override
            public Drawable newDrawable() {
                NinePatchDrawable d = new NinePatchDrawable();
                d.mNinePatch = patch;
                d.mPadding.set(padding);
                d.mTargetDensity = density;
                return d;
            }

            @Override
            public int getChangingConfigurations() {
                return 0;
            }
        };
    }
}