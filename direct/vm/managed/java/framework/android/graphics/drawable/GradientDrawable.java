package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.util.TypedValue;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** A shape (rectangle, oval, line, ring) with a solid or gradient fill and a stroke: XML <shape>. */
public class GradientDrawable extends Drawable {
    public static final int RECTANGLE = 0;
    public static final int OVAL = 1;
    public static final int LINE = 2;
    public static final int RING = 3;
    public static final int LINEAR_GRADIENT = 0;
    public static final int RADIAL_GRADIENT = 1;
    public static final int SWEEP_GRADIENT = 2;

    public enum Orientation {
        TOP_BOTTOM, TR_BL, RIGHT_LEFT, BR_TL, BOTTOM_TOP, BL_TR, LEFT_RIGHT, TL_BR
    }

    private int mShape = RECTANGLE;
    private int mGradient = LINEAR_GRADIENT;
    private Orientation mOrientation = Orientation.TOP_BOTTOM;
    private int[] mGradientColors;
    private float[] mPositions;
    private float mCenterX = 0.5f, mCenterY = 0.5f;
    private float mGradientRadius = 0.5f;
    private boolean mGradientRadiusFraction = true;
    private ColorStateList mSolidColors;
    private ColorStateList mStrokeColors;
    private int mStrokeWidth = -1;
    private float mStrokeDashWidth, mStrokeDashGap;
    private float mRadius;
    private float[] mRadiusArray;
    private Rect mPadding;
    private int mWidth = -1, mHeight = -1;
    private float mInnerRadiusRatio = 3.0f, mThicknessRatio = 9.0f;
    private int mInnerRadius = -1, mThickness = -1;
    private boolean mUseLevel;
    private boolean mUseLevelForShape = true;
    private int mAlpha = 255;
    private ColorFilter mColorFilter;

    private final Paint mFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mRect = new RectF();
    private final Path mPath = new Path();

    public GradientDrawable() {}

    public GradientDrawable(Orientation orientation, int[] colors) {
        mOrientation = orientation;
        setColors(colors);
    }

    private GradientDrawable(GradientDrawable o) {
        mShape = o.mShape;
        mGradient = o.mGradient;
        mOrientation = o.mOrientation;
        mGradientColors = o.mGradientColors != null ? o.mGradientColors.clone() : null;
        mPositions = o.mPositions != null ? o.mPositions.clone() : null;
        mCenterX = o.mCenterX;
        mCenterY = o.mCenterY;
        mGradientRadius = o.mGradientRadius;
        mGradientRadiusFraction = o.mGradientRadiusFraction;
        mSolidColors = o.mSolidColors;
        mStrokeColors = o.mStrokeColors;
        mStrokeWidth = o.mStrokeWidth;
        mStrokeDashWidth = o.mStrokeDashWidth;
        mStrokeDashGap = o.mStrokeDashGap;
        mRadius = o.mRadius;
        mRadiusArray = o.mRadiusArray != null ? o.mRadiusArray.clone() : null;
        mPadding = o.mPadding != null ? new Rect(o.mPadding) : null;
        mWidth = o.mWidth;
        mHeight = o.mHeight;
        mInnerRadiusRatio = o.mInnerRadiusRatio;
        mThicknessRatio = o.mThicknessRatio;
        mInnerRadius = o.mInnerRadius;
        mThickness = o.mThickness;
        mUseLevel = o.mUseLevel;
        mUseLevelForShape = o.mUseLevelForShape;
        mAlpha = o.mAlpha;
        mTintList = o.mTintList;
        mTintMode = o.mTintMode;
        if (o.mTintList != null) mTintFilter = updateTintFilter(mTintList, mTintMode);
    }

    // --- API --------------------------------------------------------------------

    public void setShape(int shape) {
        mShape = shape;
        invalidateSelf();
    }

    public int getShape() {
        return mShape;
    }

    public void setGradientType(int gradient) {
        mGradient = gradient;
        invalidateSelf();
    }

    public int getGradientType() {
        return mGradient;
    }

    public void setOrientation(Orientation orientation) {
        mOrientation = orientation;
        invalidateSelf();
    }

    public Orientation getOrientation() {
        return mOrientation;
    }

    public void setColors(int[] colors) {
        mGradientColors = colors != null ? colors.clone() : null;
        mPositions = null;
        mSolidColors = null;
        invalidateSelf();
    }

    public void setColors(int[] colors, float[] offsets) {
        setColors(colors);
        mPositions = offsets != null ? offsets.clone() : null;
    }

    public int[] getColors() {
        return mGradientColors != null ? mGradientColors.clone() : null;
    }

    public void setColor(int argb) {
        mSolidColors = ColorStateList.valueOf(argb);
        mGradientColors = null;
        invalidateSelf();
    }

    public void setColor(ColorStateList colorStateList) {
        mSolidColors = colorStateList;
        mGradientColors = null;
        invalidateSelf();
    }

    public ColorStateList getColor() {
        return mSolidColors;
    }

    public void setStroke(int width, int color) {
        setStroke(width, color, 0, 0);
    }

    public void setStroke(int width, ColorStateList colorStateList) {
        setStroke(width, colorStateList, 0, 0);
    }

    public void setStroke(int width, int color, float dashWidth, float dashGap) {
        setStroke(width, ColorStateList.valueOf(color), dashWidth, dashGap);
    }

    public void setStroke(int width, ColorStateList colorStateList, float dashWidth, float dashGap) {
        mStrokeWidth = width;
        mStrokeColors = colorStateList;
        mStrokeDashWidth = dashWidth;
        mStrokeDashGap = dashGap;
        invalidateSelf();
    }

    public void setCornerRadius(float radius) {
        if (radius < 0) radius = 0;
        mRadius = radius;
        mRadiusArray = null;
        invalidateSelf();
    }

    public float getCornerRadius() {
        return mRadius;
    }

    public void setCornerRadii(float[] radii) {
        mRadiusArray = radii != null ? radii.clone() : null;
        if (radii == null) mRadius = 0;
        invalidateSelf();
    }

    public float[] getCornerRadii() {
        return mRadiusArray != null ? mRadiusArray.clone() : null;
    }

    public void setSize(int width, int height) {
        mWidth = width;
        mHeight = height;
        invalidateSelf();
    }

    public void setGradientRadius(float gradientRadius) {
        mGradientRadius = gradientRadius;
        mGradientRadiusFraction = false;
        invalidateSelf();
    }

    public void setGradientCenter(float x, float y) {
        mCenterX = x;
        mCenterY = y;
        invalidateSelf();
    }

    public void setUseLevel(boolean useLevel) {
        mUseLevel = useLevel;
        invalidateSelf();
    }

    public void setPadding(int left, int top, int right, int bottom) {
        mPadding = new Rect(left, top, right, bottom);
    }

    @Override
    public boolean getPadding(Rect padding) {
        if (mPadding != null) {
            padding.set(mPadding);
            return true;
        }
        return super.getPadding(padding);
    }

    @Override
    public int getIntrinsicWidth() {
        return mWidth;
    }

    @Override
    public int getIntrinsicHeight() {
        return mHeight;
    }

    @Override
    public void setAlpha(int alpha) {
        if (alpha != mAlpha) {
            mAlpha = alpha;
            invalidateSelf();
        }
    }

    @Override
    public int getAlpha() {
        return mAlpha;
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
    public int getOpacity() {
        return mAlpha == 255 && mShape == RECTANGLE && mRadius == 0 && mRadiusArray == null && mStrokeWidth <= 0
                && mSolidColors != null && mSolidColors.isOpaque() ? PixelFormat.OPAQUE : PixelFormat.TRANSLUCENT;
    }

    @Override
    public boolean isStateful() {
        return super.isStateful() || (mSolidColors != null && mSolidColors.isStateful())
                || (mStrokeColors != null && mStrokeColors.isStateful());
    }

    @Override
    protected boolean onStateChange(int[] state) {
        return isStateful();
    }

    @Override
    protected boolean onLevelChange(int level) {
        return mUseLevel || mUseLevelForShape && mShape == RING;
    }

    @Override
    public Drawable mutate() {
        return this;
    }

    // --- drawing -------------------------------------------------------------------------

    private static int modulate(int color, int alpha) {
        int a = (color >>> 24) * (alpha + (alpha >> 7)) >> 8;
        return (color & 0xFFFFFF) | (a << 24);
    }

    @Override
    public void draw(Canvas canvas) {
        Rect b = getBounds();
        if (b.isEmpty()) return;
        final int[] state = getState();
        final ColorFilter filter = mColorFilter != null ? mColorFilter : mTintFilter;
        final boolean stroke = mStrokeWidth > 0 && mStrokeColors != null;
        final float inset = stroke ? mStrokeWidth * 0.5f : 0f;
        mRect.set(b.left + inset, b.top + inset, b.right - inset, b.bottom - inset);

        mFillPaint.setStyle(Paint.Style.FILL);
        mFillPaint.setColorFilter(filter);
        boolean fill;
        if (mGradientColors != null && mGradientColors.length >= 2) {
            mFillPaint.setColor(modulate(0xFF000000, mAlpha));
            mFillPaint.setShader(makeGradient(b));
            fill = true;
        } else {
            mFillPaint.setShader(null);
            int color = mSolidColors != null ? mSolidColors.getColorForState(state, mSolidColors.getDefaultColor()) : 0;
            mFillPaint.setColor(modulate(color, mAlpha));
            fill = (color >>> 24) != 0;
        }
        if (stroke) {
            int sc = mStrokeColors.getColorForState(state, mStrokeColors.getDefaultColor());
            mStrokePaint.setStyle(Paint.Style.STROKE);
            mStrokePaint.setStrokeWidth(mStrokeWidth);
            mStrokePaint.setColor(modulate(sc, mAlpha));
            mStrokePaint.setColorFilter(filter);
        }

        switch (mShape) {
            case RECTANGLE:
                if (mRadiusArray != null) {
                    mPath.reset();
                    mPath.addRoundRect(mRect, mRadiusArray, Path.Direction.CW);
                    if (fill) canvas.drawPath(mPath, mFillPaint);
                    if (stroke) canvas.drawPath(mPath, mStrokePaint);
                } else {
                    float rad = Math.min(mRadius, Math.min(mRect.width(), mRect.height()) * 0.5f);
                    if (fill) canvas.drawRoundRect(mRect, rad, rad, mFillPaint);
                    if (stroke) canvas.drawRoundRect(mRect, rad, rad, mStrokePaint);
                }
                break;
            case OVAL:
                if (fill) canvas.drawOval(mRect, mFillPaint);
                if (stroke) canvas.drawOval(mRect, mStrokePaint);
                break;
            case LINE: {
                if (stroke) {
                    float y = mRect.centerY();
                    canvas.drawLine(mRect.left, y, mRect.right, y, mStrokePaint);
                }
                break;
            }
            case RING: {
                float cx = b.exactCenterX(), cy = b.exactCenterY();
                float thickness = mThickness != -1 ? mThickness : b.width() / mThicknessRatio;
                float radius = mInnerRadius != -1 ? mInnerRadius : b.width() / mInnerRadiusRatio;
                RectF outer = new RectF(cx - radius - thickness, cy - radius - thickness, cx + radius + thickness,
                        cy + radius + thickness);
                float sweep = mUseLevelForShape ? 360.0f * getLevel() / 10000.0f : 360f;
                Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
                ring.setStyle(Paint.Style.STROKE);
                ring.setStrokeWidth(thickness);
                ring.setColor(mFillPaint.getColor());
                ring.setShader(mFillPaint.getShader());
                ring.setColorFilter(filter);
                RectF mid = new RectF(outer.left + thickness / 2, outer.top + thickness / 2,
                        outer.right - thickness / 2, outer.bottom - thickness / 2);
                if (sweep >= 360f) canvas.drawOval(mid, ring); else canvas.drawArc(mid, -90, sweep, false, ring);
                break;
            }
            default:
                break;
        }
    }

    private Shader makeGradient(Rect b) {
        final int[] colors = mGradientColors;
        if (mGradient == RADIAL_GRADIENT) {
            float x0 = b.left + b.width() * mCenterX;
            float y0 = b.top + b.height() * mCenterY;
            float radius = mGradientRadiusFraction ? mGradientRadius * Math.min(b.width(), b.height()) : mGradientRadius;
            return new RadialGradient(x0, y0, Math.max(radius, 0.001f), colors, mPositions, Shader.TileMode.CLAMP);
        }
        float x0, y0, x1, y1;
        RectF r = new RectF(b);
        switch (mOrientation) {
            case TOP_BOTTOM: x0 = r.left; y0 = r.top; x1 = x0; y1 = r.bottom; break;
            case TR_BL: x0 = r.right; y0 = r.top; x1 = r.left; y1 = r.bottom; break;
            case RIGHT_LEFT: x0 = r.right; y0 = r.top; x1 = r.left; y1 = y0; break;
            case BR_TL: x0 = r.right; y0 = r.bottom; x1 = r.left; y1 = r.top; break;
            case BOTTOM_TOP: x0 = r.left; y0 = r.bottom; x1 = x0; y1 = r.top; break;
            case BL_TR: x0 = r.left; y0 = r.bottom; x1 = r.right; y1 = r.top; break;
            case LEFT_RIGHT: x0 = r.left; y0 = r.top; x1 = r.right; y1 = y0; break;
            default: x0 = r.left; y0 = r.top; x1 = r.right; y1 = r.bottom; break;
        }
        return new LinearGradient(x0, y0, x1, y1, colors, mPositions, Shader.TileMode.CLAMP);
    }

    // --- XML ---------------------------------------------------------------------------------

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawable);
        mShape = a.getInt(android.R.styleable.GradientDrawable_shape, mShape);
        if (mShape == RING) {
            mInnerRadius = a.getDimensionPixelSize(android.R.styleable.GradientDrawable_innerRadius, -1);
            if (mInnerRadius == -1) {
                mInnerRadiusRatio = a.getFloat(android.R.styleable.GradientDrawable_innerRadiusRatio, mInnerRadiusRatio);
            }
            mThickness = a.getDimensionPixelSize(android.R.styleable.GradientDrawable_thickness, -1);
            if (mThickness == -1) {
                mThicknessRatio = a.getFloat(android.R.styleable.GradientDrawable_thicknessRatio, mThicknessRatio);
            }
            mUseLevelForShape = a.getBoolean(android.R.styleable.GradientDrawable_useLevel, mUseLevelForShape);
        }
        readTint(a, android.R.styleable.GradientDrawable_tint, android.R.styleable.GradientDrawable_tintMode);
        a.recycle();

        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth) continue;
            String name = parser.getName();
            switch (name) {
                case "size": {
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawableSize);
                    mWidth = a.getDimensionPixelSize(android.R.styleable.GradientDrawableSize_width, mWidth);
                    mHeight = a.getDimensionPixelSize(android.R.styleable.GradientDrawableSize_height, mHeight);
                    a.recycle();
                    break;
                }
                case "gradient":
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawableGradient);
                    inflateGradient(a);
                    a.recycle();
                    break;
                case "solid": {
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawableSolid);
                    ColorStateList c = safeColors(a, android.R.styleable.GradientDrawableSolid_color);
                    setColor(c != null ? c : ColorStateList.valueOf(0));
                    a.recycle();
                    break;
                }
                case "stroke": {
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawableStroke);
                    int width = a.getDimensionPixelSize(android.R.styleable.GradientDrawableStroke_width, -1);
                    ColorStateList c = safeColors(a, android.R.styleable.GradientDrawableStroke_color);
                    float dashWidth = a.getDimension(android.R.styleable.GradientDrawableStroke_dashWidth, 0);
                    float dashGap = a.getDimension(android.R.styleable.GradientDrawableStroke_dashGap, 0);
                    setStroke(width, c != null ? c : ColorStateList.valueOf(0), dashWidth, dashGap);
                    a.recycle();
                    break;
                }
                case "corners": {
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.DrawableCorners);
                    int radius = a.getDimensionPixelSize(android.R.styleable.DrawableCorners_radius, 0);
                    setCornerRadius(radius);
                    int tl = a.getDimensionPixelSize(android.R.styleable.DrawableCorners_topLeftRadius, radius);
                    int tr = a.getDimensionPixelSize(android.R.styleable.DrawableCorners_topRightRadius, radius);
                    int bl = a.getDimensionPixelSize(android.R.styleable.DrawableCorners_bottomLeftRadius, radius);
                    int br = a.getDimensionPixelSize(android.R.styleable.DrawableCorners_bottomRightRadius, radius);
                    if (tl != radius || tr != radius || bl != radius || br != radius) {
                        setCornerRadii(new float[] {tl, tl, tr, tr, br, br, bl, bl});
                    }
                    a.recycle();
                    break;
                }
                case "padding": {
                    a = obtainAttributes(r, theme, attrs, android.R.styleable.GradientDrawablePadding);
                    mPadding = new Rect(a.getDimensionPixelOffset(android.R.styleable.GradientDrawablePadding_left, 0),
                            a.getDimensionPixelOffset(android.R.styleable.GradientDrawablePadding_top, 0),
                            a.getDimensionPixelOffset(android.R.styleable.GradientDrawablePadding_right, 0),
                            a.getDimensionPixelOffset(android.R.styleable.GradientDrawablePadding_bottom, 0));
                    a.recycle();
                    break;
                }
                default:
                    break;
            }
        }
    }

    private static ColorStateList safeColors(TypedArray a, int index) {
        try {
            return a.getColorStateList(index);
        } catch (UnsupportedOperationException e) {
            return null;  // unresolved theme attribute
        }
    }

    private void inflateGradient(TypedArray a) {
        int start = a.getColor(android.R.styleable.GradientDrawableGradient_startColor, 0);
        boolean hasCenter = a.hasValue(android.R.styleable.GradientDrawableGradient_centerColor);
        int center = a.getColor(android.R.styleable.GradientDrawableGradient_centerColor, 0);
        int end = a.getColor(android.R.styleable.GradientDrawableGradient_endColor, 0);
        if (hasCenter) {
            mGradientColors = new int[] {start, center, end};
            mPositions = new float[] {0f, 0.5f, 1f};
        } else {
            mGradientColors = new int[] {start, end};
            mPositions = null;
        }
        mSolidColors = null;
        mCenterX = getFloatOrFraction(a, android.R.styleable.GradientDrawableGradient_centerX, mCenterX);
        mCenterY = getFloatOrFraction(a, android.R.styleable.GradientDrawableGradient_centerY, mCenterY);
        mUseLevel = a.getBoolean(android.R.styleable.GradientDrawableGradient_useLevel, mUseLevel);
        mGradient = a.getInt(android.R.styleable.GradientDrawableGradient_type, mGradient);
        int angle = (int) a.getFloat(android.R.styleable.GradientDrawableGradient_angle, 0);
        angle = ((angle % 360) + 360) % 360;
        switch (angle) {
            case 0: mOrientation = Orientation.LEFT_RIGHT; break;
            case 45: mOrientation = Orientation.BL_TR; break;
            case 90: mOrientation = Orientation.BOTTOM_TOP; break;
            case 135: mOrientation = Orientation.BR_TL; break;
            case 180: mOrientation = Orientation.RIGHT_LEFT; break;
            case 225: mOrientation = Orientation.TR_BL; break;
            case 270: mOrientation = Orientation.TOP_BOTTOM; break;
            case 315: mOrientation = Orientation.TL_BR; break;
            default: break;
        }
        TypedValue tv = a.peekValue(android.R.styleable.GradientDrawableGradient_gradientRadius);
        if (tv != null) {
            if (tv.type == TypedValue.TYPE_FRACTION) {
                mGradientRadius = tv.getFraction(1.0f, 1.0f);
                mGradientRadiusFraction = true;
            } else if (tv.type == TypedValue.TYPE_DIMENSION) {
                mGradientRadius = tv.getDimension(a.getResources().getDisplayMetrics());
                mGradientRadiusFraction = false;
            } else {
                mGradientRadius = tv.getFloat();
                mGradientRadiusFraction = false;
            }
        }
    }

    private static float getFloatOrFraction(TypedArray a, int index, float defaultValue) {
        TypedValue tv = a.peekValue(index);
        if (tv == null) return defaultValue;
        return tv.type == TypedValue.TYPE_FRACTION ? tv.getFraction(1.0f, 1.0f) : tv.getFloat();
    }

    @Override
    public ConstantState getConstantState() {
        final GradientDrawable self = this;
        return new ConstantState() {
            @Override
            public Drawable newDrawable() {
                return new GradientDrawable(self);
            }

            @Override
            public int getChangingConfigurations() {
                return 0;
            }
        };
    }
}
