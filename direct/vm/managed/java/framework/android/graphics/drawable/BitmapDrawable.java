package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.view.Gravity;
import java.io.IOException;
import java.io.InputStream;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Draws a bitmap, scaled to the target density, positioned by gravity. */
public class BitmapDrawable extends Drawable {
    private Bitmap mBitmap;
    private final Paint mPaint = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.DITHER_FLAG);
    private int mGravity = Gravity.FILL;
    private int mTargetDensity = DisplayMetrics.DENSITY_DEVICE_STABLE;
    private int mBitmapWidth = -1;
    private int mBitmapHeight = -1;
    private ColorFilter mColorFilter;
    private final Rect mDstRect = new Rect();
    private Shader.TileMode mTileX, mTileY;

    public BitmapDrawable() {}

    @Deprecated
    public BitmapDrawable(Bitmap bitmap) {
        this(null, bitmap);
    }

    public BitmapDrawable(Resources res, Bitmap bitmap) {
        if (res != null) mTargetDensity = res.getDisplayMetrics().densityDpi;
        setBitmapInternal(bitmap);
    }

    public BitmapDrawable(Resources res, InputStream is) {
        this(res, BitmapFactory.decodeStream(is));
    }

    public BitmapDrawable(Resources res, String filepath) {
        this(res, BitmapFactory.decodeFile(filepath));
    }

    private void setBitmapInternal(Bitmap bitmap) {
        mBitmap = bitmap;
        computeBitmapSize();
        invalidateSelf();
    }

    private void computeBitmapSize() {
        if (mBitmap != null) {
            mBitmapWidth = mBitmap.getScaledWidth(mTargetDensity);
            mBitmapHeight = mBitmap.getScaledHeight(mTargetDensity);
        } else {
            mBitmapWidth = mBitmapHeight = -1;
        }
    }

    public final Paint getPaint() {
        return mPaint;
    }

    public final Bitmap getBitmap() {
        return mBitmap;
    }

    public void setBitmap(Bitmap bitmap) {
        setBitmapInternal(bitmap);
    }

    public void setTargetDensity(int density) {
        mTargetDensity = density == 0 ? DisplayMetrics.DENSITY_DEFAULT : density;
        computeBitmapSize();
    }

    public void setTargetDensity(DisplayMetrics metrics) {
        setTargetDensity(metrics.densityDpi);
    }

    public int getGravity() {
        return mGravity;
    }

    public void setGravity(int gravity) {
        mGravity = gravity;
        invalidateSelf();
    }

    public void setAntiAlias(boolean aa) {
        mPaint.setAntiAlias(aa);
    }

    @Override
    public void setFilterBitmap(boolean filter) {
        mPaint.setFilterBitmap(filter);
    }

    public void setTileModeXY(Shader.TileMode xmode, Shader.TileMode ymode) {
        mTileX = xmode;
        mTileY = ymode;
    }

    @Override
    public int getIntrinsicWidth() {
        return mBitmapWidth;
    }

    @Override
    public int getIntrinsicHeight() {
        return mBitmapHeight;
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
    public ColorFilter getColorFilter() {
        return mColorFilter;
    }

    @Override
    public void draw(Canvas canvas) {
        if (mBitmap == null) return;
        Rect bounds = getBounds();
        if (mTileX != null || mTileY != null) {
            mDstRect.set(bounds);
        } else {
            Gravity.apply(mGravity, mBitmapWidth, mBitmapHeight, bounds, mDstRect, getLayoutDirection());
        }
        canvas.drawBitmap(mBitmap, null, mDstRect, mPaint);
    }

    @Override
    public int getOpacity() {
        if (mGravity != Gravity.FILL) return PixelFormat.TRANSLUCENT;
        return mBitmap == null || mBitmap.hasAlpha() || mPaint.getAlpha() < 255 ? PixelFormat.TRANSLUCENT
                : PixelFormat.OPAQUE;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.BitmapDrawable);
        mTargetDensity = r.getDisplayMetrics().densityDpi;
        final int srcResId = a.getResourceId(android.R.styleable.BitmapDrawable_src, 0);
        if (srcResId != 0) {
            Drawable src = r.getDrawable(srcResId, theme);
            if (src instanceof BitmapDrawable) {
                setBitmapInternal(((BitmapDrawable) src).getBitmap());
            } else {
                a.recycle();
                throw new XmlPullParserException(parser.getPositionDescription()
                        + ": <bitmap> requires a valid 'src' attribute");
            }
        } else if (mBitmap == null) {
            a.recycle();
            throw new XmlPullParserException(parser.getPositionDescription()
                    + ": <bitmap> requires a valid 'src' attribute");
        }
        mGravity = a.getInt(android.R.styleable.BitmapDrawable_gravity, mGravity);
        mPaint.setAntiAlias(a.getBoolean(android.R.styleable.BitmapDrawable_antialias, mPaint.isAntiAlias()));
        mPaint.setFilterBitmap(a.getBoolean(android.R.styleable.BitmapDrawable_filter, true));
        int tileMode = a.getInt(android.R.styleable.BitmapDrawable_tileMode, -1);
        if (tileMode >= 0) {
            Shader.TileMode m = tileMode == 0 ? Shader.TileMode.CLAMP : tileMode == 1 ? Shader.TileMode.REPEAT
                    : Shader.TileMode.MIRROR;
            setTileModeXY(m, m);
        }
        readTint(a, android.R.styleable.BitmapDrawable_tint, android.R.styleable.BitmapDrawable_tintMode);
        a.recycle();
    }

    @Override
    public ConstantState getConstantState() {
        final Bitmap bitmap = mBitmap;
        final int gravity = mGravity;
        return new ConstantState() {
            @Override
            public Drawable newDrawable() {
                return newDrawable(null);
            }

            @Override
            public Drawable newDrawable(Resources res) {
                BitmapDrawable d = new BitmapDrawable(res, bitmap);
                d.mGravity = gravity;
                return d;
            }

            @Override
            public int getChangingConfigurations() {
                return 0;
            }
        };
    }
}