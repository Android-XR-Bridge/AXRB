package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Insets;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.StateSet;
import android.util.TypedValue;
import java.io.IOException;
import java.io.InputStream;
import java.lang.ref.WeakReference;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Something that can be drawn: the base of all drawables. */
public abstract class Drawable {
    private static final Rect ZERO_BOUNDS_RECT = new Rect();

    private int[] mStateSet = StateSet.WILD_CARD;
    private int mLevel;
    private int mChangingConfigurations;
    private Rect mBounds = ZERO_BOUNDS_RECT;
    private WeakReference<Callback> mCallback;
    private boolean mVisible = true;
    private int mLayoutDirection;
    int mSrcDensityOverride;

    ColorStateList mTintList;
    PorterDuff.Mode mTintMode = PorterDuff.Mode.SRC_IN;
    PorterDuffColorFilter mTintFilter;

    public interface Callback {
        void invalidateDrawable(Drawable who);

        void scheduleDrawable(Drawable who, Runnable what, long when);

        void unscheduleDrawable(Drawable who, Runnable what);
    }

    public abstract static class ConstantState {
        public abstract Drawable newDrawable();

        public Drawable newDrawable(Resources res) {
            return newDrawable();
        }

        public Drawable newDrawable(Resources res, Resources.Theme theme) {
            return newDrawable(res);
        }

        public abstract int getChangingConfigurations();

        public boolean canApplyTheme() {
            return false;
        }
    }

    public abstract void draw(Canvas canvas);

    // --- bounds ----------------------------------------------------------------

    public void setBounds(int left, int top, int right, int bottom) {
        Rect oldBounds = mBounds;
        if (oldBounds == ZERO_BOUNDS_RECT) oldBounds = mBounds = new Rect();
        if (oldBounds.left != left || oldBounds.top != top || oldBounds.right != right || oldBounds.bottom != bottom) {
            if (!oldBounds.isEmpty()) invalidateSelf();
            mBounds.set(left, top, right, bottom);
            onBoundsChange(mBounds);
        }
    }

    public void setBounds(Rect bounds) {
        setBounds(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }

    public final void copyBounds(Rect bounds) {
        bounds.set(mBounds);
    }

    public final Rect copyBounds() {
        return new Rect(mBounds);
    }

    public final Rect getBounds() {
        if (mBounds == ZERO_BOUNDS_RECT) mBounds = new Rect();
        return mBounds;
    }

    public Rect getDirtyBounds() {
        return getBounds();
    }

    protected void onBoundsChange(Rect bounds) {}

    // --- configuration and callbacks ------------------------------------------------

    public void setChangingConfigurations(int configs) {
        mChangingConfigurations = configs;
    }

    public int getChangingConfigurations() {
        return mChangingConfigurations;
    }

    public void setDither(boolean dither) {}

    public void setFilterBitmap(boolean filter) {}

    public boolean isFilterBitmap() {
        return false;
    }

    public final void setCallback(Callback cb) {
        mCallback = cb != null ? new WeakReference<Callback>(cb) : null;
    }

    public Callback getCallback() {
        return mCallback != null ? mCallback.get() : null;
    }

    public void invalidateSelf() {
        final Callback callback = getCallback();
        if (callback != null) callback.invalidateDrawable(this);
    }

    public void scheduleSelf(Runnable what, long when) {
        final Callback callback = getCallback();
        if (callback != null) callback.scheduleDrawable(this, what, when);
    }

    public void unscheduleSelf(Runnable what) {
        final Callback callback = getCallback();
        if (callback != null) callback.unscheduleDrawable(this, what);
    }

    public int getLayoutDirection() {
        return mLayoutDirection;
    }

    public final boolean setLayoutDirection(int layoutDirection) {
        if (mLayoutDirection != layoutDirection) {
            mLayoutDirection = layoutDirection;
            return onLayoutDirectionChanged(layoutDirection);
        }
        return false;
    }

    public boolean onLayoutDirectionChanged(int layoutDirection) {
        return false;
    }

    // --- alpha, filters, tint ----------------------------------------------------------

    public abstract void setAlpha(int alpha);

    public int getAlpha() {
        return 0xFF;
    }

    public abstract void setColorFilter(ColorFilter colorFilter);

    @Deprecated
    public void setColorFilter(int color, PorterDuff.Mode mode) {
        setColorFilter(new PorterDuffColorFilter(color, mode));
    }

    public void clearColorFilter() {
        setColorFilter(null);
    }

    public ColorFilter getColorFilter() {
        return null;
    }

    public void setTint(int tintColor) {
        setTintList(ColorStateList.valueOf(tintColor));
    }

    public void setTintList(ColorStateList tint) {
        mTintList = tint;
        mTintFilter = updateTintFilter(tint, mTintMode);
        invalidateSelf();
    }

    public void setTintMode(PorterDuff.Mode tintMode) {
        mTintMode = tintMode != null ? tintMode : PorterDuff.Mode.SRC_IN;
        mTintFilter = updateTintFilter(mTintList, mTintMode);
        invalidateSelf();
    }

    /** The filter tinting this drawable for its current state, or null. */
    PorterDuffColorFilter updateTintFilter(ColorStateList tint, PorterDuff.Mode tintMode) {
        if (tint == null || tintMode == null) return null;
        final int color = tint.getColorForState(getState(), tint.getDefaultColor());
        return new PorterDuffColorFilter(color, tintMode);
    }

    // --- state and level -------------------------------------------------------------------

    public boolean isStateful() {
        return mTintList != null && mTintList.isStateful();
    }

    public boolean hasFocusStateSpecified() {
        return false;
    }

    public boolean setState(final int[] stateSet) {
        if (!java.util.Arrays.equals(mStateSet, stateSet)) {
            mStateSet = stateSet;
            boolean changed = onStateChange(stateSet);
            if (mTintList != null && mTintList.isStateful()) {
                mTintFilter = updateTintFilter(mTintList, mTintMode);
                changed = true;
            }
            return changed;
        }
        return false;
    }

    public int[] getState() {
        return mStateSet;
    }

    public void jumpToCurrentState() {}

    public Drawable getCurrent() {
        return this;
    }

    public final boolean setLevel(int level) {
        if (mLevel != level) {
            mLevel = level;
            return onLevelChange(level);
        }
        return false;
    }

    public final int getLevel() {
        return mLevel;
    }

    protected boolean onStateChange(int[] state) {
        return false;
    }

    protected boolean onLevelChange(int level) {
        return false;
    }

    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = mVisible != visible;
        if (changed) {
            mVisible = visible;
            invalidateSelf();
        }
        return changed;
    }

    public final boolean isVisible() {
        return mVisible;
    }

    public void setAutoMirrored(boolean mirrored) {}

    public boolean isAutoMirrored() {
        return false;
    }

    public void setHotspot(float x, float y) {}

    public void setHotspotBounds(int left, int top, int right, int bottom) {}

    // --- size -----------------------------------------------------------------------------

    public abstract int getOpacity();

    public static int resolveOpacity(int op1, int op2) {
        if (op1 == op2) return op1;
        if (op1 == PixelFormat.UNKNOWN || op2 == PixelFormat.UNKNOWN) return PixelFormat.UNKNOWN;
        if (op1 == PixelFormat.TRANSLUCENT || op2 == PixelFormat.TRANSLUCENT) return PixelFormat.TRANSLUCENT;
        if (op1 == PixelFormat.TRANSPARENT || op2 == PixelFormat.TRANSPARENT) return PixelFormat.TRANSPARENT;
        return PixelFormat.OPAQUE;
    }

    public int getIntrinsicWidth() {
        return -1;
    }

    public int getIntrinsicHeight() {
        return -1;
    }

    public int getMinimumWidth() {
        final int intrinsicWidth = getIntrinsicWidth();
        return intrinsicWidth > 0 ? intrinsicWidth : 0;
    }

    public int getMinimumHeight() {
        final int intrinsicHeight = getIntrinsicHeight();
        return intrinsicHeight > 0 ? intrinsicHeight : 0;
    }

    public boolean getPadding(Rect padding) {
        padding.set(0, 0, 0, 0);
        return false;
    }

    public Insets getOpticalInsets() {
        return Insets.NONE;
    }

    public Drawable mutate() {
        return this;
    }

    public ConstantState getConstantState() {
        return null;
    }

    // --- themes ---------------------------------------------------------------------------

    public void applyTheme(Resources.Theme t) {}

    public boolean canApplyTheme() {
        return false;
    }

    // --- creation ---------------------------------------------------------------------------

    public static Drawable createFromStream(InputStream is, String srcName) {
        if (is == null) return null;
        Rect pad = new Rect();
        Bitmap bm = BitmapFactory.decodeStream(is, pad, null);
        if (bm == null) return null;
        byte[] np = bm.getNinePatchChunk();
        if (np != null) return new NinePatchDrawable(null, bm, np, pad, srcName);
        return new BitmapDrawable(null, bm);
    }

    public static Drawable createFromResourceStream(Resources res, TypedValue value, InputStream is, String srcName) {
        return createFromResourceStream(res, value, is, srcName, null);
    }

    public static Drawable createFromResourceStream(Resources res, TypedValue value, InputStream is, String srcName,
            BitmapFactory.Options opts) {
        if (is == null) return null;
        Rect pad = new Rect();
        if (opts == null) opts = new BitmapFactory.Options();
        opts.inScreenDensity = res != null ? res.getDisplayMetrics().densityDpi : 0;
        Bitmap bm = BitmapFactory.decodeResourceStream(res, value, is, pad, opts);
        if (bm == null) return null;
        byte[] np = bm.getNinePatchChunk();
        if (np != null) return new NinePatchDrawable(res, bm, np, pad, srcName);
        return new BitmapDrawable(res, bm);
    }

    /** Hidden: a drawable from resource file bytes (PNG, JPEG, ...). */
    public static Drawable createFromResourceBytes(Resources res, TypedValue value, byte[] bytes, String srcName) {
        return createFromResourceStream(res, value, new java.io.ByteArrayInputStream(bytes), srcName, null);
    }

    public static Drawable createFromXml(Resources r, XmlPullParser parser) throws XmlPullParserException, IOException {
        return createFromXml(r, parser, null);
    }

    public static Drawable createFromXml(Resources r, XmlPullParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        return createFromXmlForDensity(r, parser, 0, theme);
    }

    /** Hidden: as createFromXml, for a density override (0 = none). */
    public static Drawable createFromXmlForDensity(Resources r, XmlPullParser parser, int density,
            Resources.Theme theme) throws XmlPullParserException, IOException {
        AttributeSet attrs = (AttributeSet) parser;
        int type;
        while ((type = parser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
            // skip to the root element
        }
        if (type != XmlPullParser.START_TAG) throw new XmlPullParserException("No start tag found");
        Drawable drawable = createFromXmlInner(r, parser, attrs, theme);
        if (drawable == null) {
            throw new RuntimeException("Unknown initial tag: " + parser.getName());
        }
        return drawable;
    }

    public static Drawable createFromXmlInner(Resources r, XmlPullParser parser, AttributeSet attrs)
            throws XmlPullParserException, IOException {
        return createFromXmlInner(r, parser, attrs, null);
    }

    public static Drawable createFromXmlInner(Resources r, XmlPullParser parser, AttributeSet attrs,
            Resources.Theme theme) throws XmlPullParserException, IOException {
        final String name = parser.getName();
        Drawable drawable;
        switch (name) {
            case "selector":
            case "animated-selector":
                drawable = new StateListDrawable();
                break;
            case "level-list":
                drawable = new LevelListDrawable();
                break;
            case "layer-list":
                drawable = new LayerDrawable();
                break;
            case "transition":
                drawable = new TransitionDrawable();
                break;
            case "ripple":
                drawable = new RippleDrawable();
                break;
            case "adaptive-icon":
                drawable = new AdaptiveIconDrawable();
                break;
            case "color":
                drawable = new ColorDrawable();
                break;
            case "shape":
                drawable = new GradientDrawable();
                break;
            case "vector":
            case "animated-vector":
                drawable = new VectorDrawable();
                break;
            case "scale":
                drawable = new ScaleDrawable();
                break;
            case "clip":
                drawable = new ClipDrawable();
                break;
            case "rotate":
            case "animated-rotate":
                drawable = new RotateDrawable();
                break;
            case "animation-list":
                drawable = new AnimationDrawable();
                break;
            case "inset":
                drawable = new InsetDrawable();
                break;
            case "bitmap":
                drawable = new BitmapDrawable();
                break;
            case "nine-patch":
                drawable = new NinePatchDrawable();
                break;
            default:
                if (name.indexOf('.') > 0) {
                    try {
                        drawable = (Drawable) Class.forName(name).newInstance();
                    } catch (Exception e) {
                        throw new XmlPullParserException(parser.getPositionDescription()
                                + ": error creating drawable class " + name, parser, e);
                    }
                    break;
                }
                throw new XmlPullParserException(parser.getPositionDescription() + ": invalid drawable tag " + name);
        }
        drawable.inflate(r, parser, attrs, theme);
        return drawable;
    }

    public static Drawable createFromPath(String pathName) {
        if (pathName == null) return null;
        Bitmap bm = BitmapFactory.decodeFile(pathName);
        return bm != null ? new BitmapDrawable(null, bm) : null;
    }

    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs)
            throws XmlPullParserException, IOException {
        inflate(r, parser, attrs, null);
    }

    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.Drawable);
        mVisible = a.getBoolean(android.R.styleable.Drawable_visible, mVisible);
        a.recycle();
    }

    static TypedArray obtainAttributes(Resources res, Resources.Theme theme, AttributeSet set, int[] attrs) {
        if (theme == null) return res.obtainAttributes(set, attrs);
        return theme.obtainStyledAttributes(set, attrs, 0, 0);
    }

    /** Reads the android:tint / android:tintMode pair of a drawable element. */
    void readTint(TypedArray a, int tintIndex, int tintModeIndex) {
        if (a.hasValue(tintModeIndex)) mTintMode = parseTintMode(a.getInt(tintModeIndex, -1), PorterDuff.Mode.SRC_IN);
        if (a.hasValue(tintIndex)) {
            try {
                ColorStateList tint = a.getColorStateList(tintIndex);
                if (tint != null) setTintList(tint);
            } catch (UnsupportedOperationException e) {
                // an unresolved theme attribute: no tint
            }
        }
    }

    public static PorterDuff.Mode parseTintMode(int value, PorterDuff.Mode defaultMode) {
        switch (value) {
            case 3: return PorterDuff.Mode.SRC_OVER;
            case 5: return PorterDuff.Mode.SRC_IN;
            case 9: return PorterDuff.Mode.SRC_ATOP;
            case 14: return PorterDuff.Mode.MULTIPLY;
            case 15: return PorterDuff.Mode.SCREEN;
            case 16: return PorterDuff.Mode.ADD;
            default: return defaultMode;
        }
    }

    /** Inflates the child elements of `parser`'s current tag into drawables, for containers. */
    static Drawable inflateChild(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        int type;
        while ((type = parser.next()) == XmlPullParser.TEXT) {
            // skip whitespace
        }
        if (type != XmlPullParser.START_TAG) {
            throw new XmlPullParserException(parser.getPositionDescription()
                    + ": <item> tag requires a 'drawable' attribute or child tag defining a drawable");
        }
        return createFromXmlInner(r, parser, attrs, theme);
    }
}
