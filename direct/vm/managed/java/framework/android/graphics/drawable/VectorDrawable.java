package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.util.PathParser;
import android.util.TypedValue;
import java.io.IOException;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * A vector graphic: XML <vector> with groups, paths and clip paths, drawn with
 * the renderer's path support. <animated-vector> is drawn as its static target.
 */
public class VectorDrawable extends Drawable {
    private float mViewportWidth = 0, mViewportHeight = 0;
    private int mBaseWidth = 0, mBaseHeight = 0;
    private float mRootAlpha = 1.0f;
    private boolean mAutoMirrored;
    private ColorFilter mColorFilter;
    private int mAlpha = 255;
    private final Group mRoot = new Group();

    static class Node {}

    static final class Group extends Node {
        final ArrayList<Node> children = new ArrayList<Node>();
        float rotate, pivotX, pivotY, scaleX = 1, scaleY = 1, translateX, translateY;
        String name;
    }

    static final class VPath extends Node {
        Path path;
        boolean clip;
        ColorStateList fillColor;
        Shader fillShader;
        ColorStateList strokeColor;
        Shader strokeShader;
        float strokeWidth;
        float strokeAlpha = 1, fillAlpha = 1;
        Paint.Cap cap = Paint.Cap.BUTT;
        Paint.Join join = Paint.Join.MITER;
        float miter = 4;
        boolean evenOdd;
        String name;
    }

    public VectorDrawable() {}

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);

    @Override
    public int getIntrinsicWidth() {
        return mBaseWidth;
    }

    @Override
    public int getIntrinsicHeight() {
        return mBaseHeight;
    }

    @Override
    public void setAlpha(int alpha) {
        if (mAlpha != alpha) {
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
        return PixelFormat.TRANSLUCENT;
    }

    @Override
    public void setAutoMirrored(boolean mirrored) {
        mAutoMirrored = mirrored;
    }

    @Override
    public boolean isAutoMirrored() {
        return mAutoMirrored;
    }

    @Override
    public boolean isStateful() {
        return super.isStateful() || isStateful(mRoot);
    }

    private static boolean isStateful(Group g) {
        for (Node n : g.children) {
            if (n instanceof Group && isStateful((Group) n)) return true;
            if (n instanceof VPath) {
                VPath p = (VPath) n;
                if ((p.fillColor != null && p.fillColor.isStateful()) || (p.strokeColor != null && p.strokeColor.isStateful())) {
                    return true;
                }
            }
        }
        return false;
    }

    @Override
    protected boolean onStateChange(int[] stateSet) {
        return isStateful();
    }

    @Override
    public void draw(Canvas canvas) {
        final Rect b = getBounds();
        if (b.width() <= 0 || b.height() <= 0 || mViewportWidth <= 0 || mViewportHeight <= 0) return;
        final int save = canvas.save();
        canvas.translate(b.left, b.top);
        if (mAutoMirrored && getLayoutDirection() == 1) {
            canvas.translate(b.width(), 0);
            canvas.scale(-1.0f, 1.0f);
        }
        canvas.clipRect(0, 0, b.width(), b.height());
        canvas.scale(b.width() / mViewportWidth, b.height() / mViewportHeight);
        final ColorFilter filter = mColorFilter != null ? mColorFilter : mTintFilter;
        final float alpha = mRootAlpha * (mAlpha / 255f);
        drawGroup(canvas, mRoot, filter, alpha, getState());
        canvas.restoreToCount(save);
    }

    private void drawGroup(Canvas canvas, Group g, ColorFilter filter, float alpha, int[] state) {
        final int save = canvas.save();
        canvas.translate(g.translateX + g.pivotX, g.translateY + g.pivotY);
        canvas.rotate(g.rotate);
        canvas.scale(g.scaleX, g.scaleY);
        canvas.translate(-g.pivotX, -g.pivotY);
        for (Node n : g.children) {
            if (n instanceof Group) {
                drawGroup(canvas, (Group) n, filter, alpha, state);
            } else {
                VPath p = (VPath) n;
                if (p.clip) {
                    canvas.clipPath(p.path);
                    continue;
                }
                if (p.fillColor != null || p.fillShader != null) {
                    int color = p.fillColor != null ? p.fillColor.getColorForState(state, p.fillColor.getDefaultColor())
                            : 0xFF000000;
                    mFill.setStyle(Paint.Style.FILL);
                    mFill.setShader(p.fillShader);
                    mFill.setColor(applyAlpha(color, p.fillAlpha * alpha));
                    mFill.setColorFilter(filter);
                    p.path.setFillType(p.evenOdd ? Path.FillType.EVEN_ODD : Path.FillType.WINDING);
                    if ((mFill.getColor() >>> 24) != 0) canvas.drawPath(p.path, mFill);
                }
                if ((p.strokeColor != null || p.strokeShader != null) && p.strokeWidth > 0) {
                    int color = p.strokeColor != null
                            ? p.strokeColor.getColorForState(state, p.strokeColor.getDefaultColor()) : 0xFF000000;
                    mStroke.setStyle(Paint.Style.STROKE);
                    mStroke.setShader(p.strokeShader);
                    mStroke.setColor(applyAlpha(color, p.strokeAlpha * alpha));
                    mStroke.setStrokeWidth(p.strokeWidth);
                    mStroke.setStrokeCap(p.cap);
                    mStroke.setStrokeJoin(p.join);
                    mStroke.setStrokeMiter(p.miter);
                    mStroke.setColorFilter(filter);
                    if ((mStroke.getColor() >>> 24) != 0) canvas.drawPath(p.path, mStroke);
                }
            }
        }
        canvas.restoreToCount(save);
    }

    private static int applyAlpha(int color, float alpha) {
        int a = (int) ((color >>> 24) * Math.max(0f, Math.min(1f, alpha)) + 0.5f);
        return (color & 0xFFFFFF) | (a << 24);
    }

    // --- XML ------------------------------------------------------------------------

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        if ("animated-vector".equals(parser.getName())) {
            inflateAnimated(r, parser, attrs, theme);
            return;
        }
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.VectorDrawable);
        mViewportWidth = a.getFloat(android.R.styleable.VectorDrawable_viewportWidth, mViewportWidth);
        mViewportHeight = a.getFloat(android.R.styleable.VectorDrawable_viewportHeight, mViewportHeight);
        if (mViewportWidth <= 0 || mViewportHeight <= 0) {
            a.recycle();
            throw new XmlPullParserException(a.getPositionDescription()
                    + "<vector> tag requires viewportWidth and viewportHeight > 0");
        }
        mBaseWidth = a.getDimensionPixelSize(android.R.styleable.VectorDrawable_width, mBaseWidth);
        mBaseHeight = a.getDimensionPixelSize(android.R.styleable.VectorDrawable_height, mBaseHeight);
        if (mBaseWidth <= 0 || mBaseHeight <= 0) {
            a.recycle();
            throw new XmlPullParserException(a.getPositionDescription()
                    + "<vector> tag requires width and height > 0");
        }
        mRootAlpha = a.getFloat(android.R.styleable.VectorDrawable_alpha, mRootAlpha);
        mAutoMirrored = a.getBoolean(android.R.styleable.VectorDrawable_autoMirrored, mAutoMirrored);
        readTint(a, android.R.styleable.VectorDrawable_tint, android.R.styleable.VectorDrawable_tintMode);
        a.recycle();

        ArrayList<Group> stack = new ArrayList<Group>();
        stack.add(mRoot);
        final int innerDepth = parser.getDepth() + 1;
        int type;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && (parser.getDepth() >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type == XmlPullParser.START_TAG) {
                final String name = parser.getName();
                final Group current = stack.get(stack.size() - 1);
                if ("path".equals(name)) {
                    current.children.add(inflatePath(r, attrs, theme, false));
                } else if ("clip-path".equals(name)) {
                    current.children.add(inflatePath(r, attrs, theme, true));
                } else if ("group".equals(name)) {
                    Group g = inflateGroup(r, attrs, theme);
                    current.children.add(g);
                    stack.add(g);
                }
            } else if (type == XmlPullParser.END_TAG && "group".equals(parser.getName()) && stack.size() > 1) {
                stack.remove(stack.size() - 1);
            }
        }
    }

    /** <animated-vector android:drawable="@drawable/v">: draw the target vector as is. */
    private void inflateAnimated(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.AnimatedVectorDrawable);
        final int res = a.getResourceId(android.R.styleable.AnimatedVectorDrawable_drawable, 0);
        a.recycle();
        if (res == 0) throw new XmlPullParserException(parser.getPositionDescription() + ": missing drawable");
        Drawable target = r.getDrawable(res, theme);
        if (!(target instanceof VectorDrawable)) {
            throw new XmlPullParserException(parser.getPositionDescription() + ": target is not a vector");
        }
        VectorDrawable v = (VectorDrawable) target;
        mViewportWidth = v.mViewportWidth;
        mViewportHeight = v.mViewportHeight;
        mBaseWidth = v.mBaseWidth;
        mBaseHeight = v.mBaseHeight;
        mRootAlpha = v.mRootAlpha;
        mAutoMirrored = v.mAutoMirrored;
        mRoot.children.addAll(v.mRoot.children);
        mTintList = v.mTintList;
        mTintMode = v.mTintMode;
        mTintFilter = v.mTintFilter;
    }

    private Group inflateGroup(Resources r, AttributeSet attrs, Resources.Theme theme) {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.VectorDrawableGroup);
        Group g = new Group();
        g.rotate = a.getFloat(android.R.styleable.VectorDrawableGroup_rotation, 0);
        g.pivotX = a.getFloat(android.R.styleable.VectorDrawableGroup_pivotX, 0);
        g.pivotY = a.getFloat(android.R.styleable.VectorDrawableGroup_pivotY, 0);
        g.scaleX = a.getFloat(android.R.styleable.VectorDrawableGroup_scaleX, 1);
        g.scaleY = a.getFloat(android.R.styleable.VectorDrawableGroup_scaleY, 1);
        g.translateX = a.getFloat(android.R.styleable.VectorDrawableGroup_translateX, 0);
        g.translateY = a.getFloat(android.R.styleable.VectorDrawableGroup_translateY, 0);
        g.name = a.getString(android.R.styleable.VectorDrawableGroup_name);
        a.recycle();
        return g;
    }

    private VPath inflatePath(Resources r, AttributeSet attrs, Resources.Theme theme, boolean clip) {
        VPath p = new VPath();
        p.clip = clip;
        if (clip) {
            final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.VectorDrawableClipPath);
            p.path = PathParser.createPathFromPathData(a.getString(android.R.styleable.VectorDrawableClipPath_pathData));
            p.name = a.getString(android.R.styleable.VectorDrawableClipPath_name);
            a.recycle();
            return p;
        }
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.VectorDrawablePath);
        p.name = a.getString(android.R.styleable.VectorDrawablePath_name);
        p.path = PathParser.createPathFromPathData(a.getString(android.R.styleable.VectorDrawablePath_pathData));
        Object fill = complexColor(r, theme, a, android.R.styleable.VectorDrawablePath_fillColor);
        if (fill instanceof Shader) p.fillShader = (Shader) fill; else p.fillColor = (ColorStateList) fill;
        Object stroke = complexColor(r, theme, a, android.R.styleable.VectorDrawablePath_strokeColor);
        if (stroke instanceof Shader) p.strokeShader = (Shader) stroke; else p.strokeColor = (ColorStateList) stroke;
        p.strokeWidth = a.getFloat(android.R.styleable.VectorDrawablePath_strokeWidth, 0);
        p.strokeAlpha = a.getFloat(android.R.styleable.VectorDrawablePath_strokeAlpha, 1);
        p.fillAlpha = a.getFloat(android.R.styleable.VectorDrawablePath_fillAlpha, 1);
        int cap = a.getInt(android.R.styleable.VectorDrawablePath_strokeLineCap, 0);
        p.cap = cap == 1 ? Paint.Cap.ROUND : cap == 2 ? Paint.Cap.SQUARE : Paint.Cap.BUTT;
        int join = a.getInt(android.R.styleable.VectorDrawablePath_strokeLineJoin, 0);
        p.join = join == 1 ? Paint.Join.ROUND : join == 2 ? Paint.Join.BEVEL : Paint.Join.MITER;
        p.miter = a.getFloat(android.R.styleable.VectorDrawablePath_strokeMiterLimit, 4);
        p.evenOdd = a.getInt(android.R.styleable.VectorDrawablePath_fillType, 0) == 1;
        a.recycle();
        return p;
    }

    /** A color, color state list, or gradient (<gradient> color resource) as a Shader; null if absent. */
    private static Object complexColor(Resources r, Resources.Theme theme, TypedArray a, int index) {
        TypedValue v = a.peekValue(index);
        if (v == null) return null;
        if (v.type >= TypedValue.TYPE_FIRST_COLOR_INT && v.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            return ColorStateList.valueOf(v.data);
        }
        if (v.type == TypedValue.TYPE_STRING && v.string != null && v.string.toString().endsWith(".xml")) {
            try (XmlResourceParser rp = r.loadXmlResourceParser(v.string.toString(), v.resourceId, v.assetCookie,
                    "color")) {
                int type;
                while ((type = rp.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
                    // find the root
                }
                if (type == XmlPullParser.START_TAG && "gradient".equals(rp.getName())) {
                    return GradientColor.createShader(r, rp, theme);
                }
            } catch (Exception e) {
                // fall through to a color state list
            }
        }
        try {
            return a.getColorStateList(index);
        } catch (RuntimeException e) {
            return null;
        }
    }

    /** res/color <gradient> elements (API 24) as shaders. */
    static final class GradientColor {
        static Shader createShader(Resources r, XmlResourceParser parser, Resources.Theme theme)
                throws XmlPullParserException, IOException {
            final TypedArray a = obtainAttributes(r, theme, parser, android.R.styleable.GradientColor);
            float startX = a.getFloat(android.R.styleable.GradientColor_startX, 0);
            float startY = a.getFloat(android.R.styleable.GradientColor_startY, 0);
            float endX = a.getFloat(android.R.styleable.GradientColor_endX, 0);
            float endY = a.getFloat(android.R.styleable.GradientColor_endY, 0);
            float centerX = a.getFloat(android.R.styleable.GradientColor_centerX, 0);
            float centerY = a.getFloat(android.R.styleable.GradientColor_centerY, 0);
            int type = a.getInt(android.R.styleable.GradientColor_type, 0);
            int startColor = a.getColor(android.R.styleable.GradientColor_startColor, 0);
            boolean hasCenter = a.hasValue(android.R.styleable.GradientColor_centerColor);
            int centerColor = a.getColor(android.R.styleable.GradientColor_centerColor, 0);
            int endColor = a.getColor(android.R.styleable.GradientColor_endColor, 0);
            int tile = a.getInt(android.R.styleable.GradientColor_tileMode, 0);
            float radius = a.getFloat(android.R.styleable.GradientColor_gradientRadius, 0);
            a.recycle();
            ArrayList<Integer> colors = new ArrayList<Integer>();
            ArrayList<Float> offsets = new ArrayList<Float>();
            final int innerDepth = parser.getDepth() + 1;
            int t;
            int depth;
            while ((t = parser.next()) != XmlPullParser.END_DOCUMENT
                    && ((depth = parser.getDepth()) >= innerDepth || t != XmlPullParser.END_TAG)) {
                if (t != XmlPullParser.START_TAG || depth > innerDepth || !parser.getName().equals("item")) continue;
                final TypedArray ia = obtainAttributes(r, theme, parser, android.R.styleable.GradientColorItem);
                colors.add(ia.getColor(android.R.styleable.GradientColorItem_color, 0));
                offsets.add(ia.getFloat(android.R.styleable.GradientColorItem_offset, 0));
                ia.recycle();
            }
            int[] c;
            float[] o = null;
            if (colors.size() >= 2) {
                c = new int[colors.size()];
                o = new float[colors.size()];
                for (int i = 0; i < c.length; i++) {
                    c[i] = colors.get(i);
                    o[i] = offsets.get(i);
                }
            } else if (hasCenter) {
                c = new int[] {startColor, centerColor, endColor};
                o = new float[] {0f, 0.5f, 1f};
            } else {
                c = new int[] {startColor, endColor};
            }
            Shader.TileMode mode = tile == 1 ? Shader.TileMode.REPEAT : tile == 2 ? Shader.TileMode.MIRROR
                    : Shader.TileMode.CLAMP;
            if (type == 1 && radius > 0) return new RadialGradient(centerX, centerY, radius, c, o, mode);
            return new LinearGradient(startX, startY, endX, endY, c, o, mode);
        }
    }

    @Override
    public ConstantState getConstantState() {
        final VectorDrawable self = this;
        return new ConstantState() {
            @Override
            public Drawable newDrawable() {
                VectorDrawable d = new VectorDrawable();
                d.mViewportWidth = self.mViewportWidth;
                d.mViewportHeight = self.mViewportHeight;
                d.mBaseWidth = self.mBaseWidth;
                d.mBaseHeight = self.mBaseHeight;
                d.mRootAlpha = self.mRootAlpha;
                d.mAutoMirrored = self.mAutoMirrored;
                d.mRoot.children.addAll(self.mRoot.children);
                d.mTintList = self.mTintList;
                d.mTintMode = self.mTintMode;
                d.mTintFilter = self.mTintFilter;
                return d;
            }

            @Override
            public int getChangingConfigurations() {
                return 0;
            }
        };
    }
}
