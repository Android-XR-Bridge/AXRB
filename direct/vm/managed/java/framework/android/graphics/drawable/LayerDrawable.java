package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.Gravity;
import java.io.IOException;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Draws its layers in order, each inset within the bounds: XML <layer-list>. */
public class LayerDrawable extends Drawable implements Drawable.Callback {
    public static final int PADDING_MODE_NEST = 0;
    public static final int PADDING_MODE_STACK = 1;
    public static final int INSET_UNDEFINED = Integer.MIN_VALUE;

    static final class Layer {
        Drawable drawable;
        int id = -1;
        int insetL, insetT, insetR, insetB;
        int width = -1, height = -1;
        int gravity = Gravity.NO_GRAVITY;
    }

    final ArrayList<Layer> mLayers = new ArrayList<Layer>();
    private int mPaddingMode = PADDING_MODE_NEST;
    private int mAlpha = 255;
    private final Rect mTmp = new Rect();

    public LayerDrawable(Drawable[] layers) {
        for (Drawable d : layers) addLayer(d);
    }

    LayerDrawable() {}

    public int addLayer(Drawable dr) {
        Layer l = new Layer();
        l.drawable = dr;
        dr.setCallback(this);
        mLayers.add(l);
        updateLayerBounds(getBounds());
        return mLayers.size() - 1;
    }

    public int getNumberOfLayers() {
        return mLayers.size();
    }

    public Drawable getDrawable(int index) {
        return mLayers.get(index).drawable;
    }

    public int getId(int index) {
        return mLayers.get(index).id;
    }

    public void setId(int index, int id) {
        mLayers.get(index).id = id;
    }

    public Drawable findDrawableByLayerId(int id) {
        for (int i = mLayers.size() - 1; i >= 0; i--) {
            if (mLayers.get(i).id == id) return mLayers.get(i).drawable;
        }
        return null;
    }

    public int findIndexByLayerId(int id) {
        for (int i = 0; i < mLayers.size(); i++) {
            if (mLayers.get(i).id == id) return i;
        }
        return -1;
    }

    public boolean setDrawableByLayerId(int id, Drawable drawable) {
        int index = findIndexByLayerId(id);
        if (index < 0) return false;
        setDrawable(index, drawable);
        return true;
    }

    public void setDrawable(int index, Drawable drawable) {
        Layer l = mLayers.get(index);
        if (l.drawable != null) l.drawable.setCallback(null);
        l.drawable = drawable;
        if (drawable != null) {
            drawable.setCallback(this);
            drawable.setState(getState());
            drawable.setLevel(getLevel());
        }
        updateLayerBounds(getBounds());
        invalidateSelf();
    }

    public void setLayerInset(int index, int l, int t, int r, int b) {
        Layer layer = mLayers.get(index);
        layer.insetL = l;
        layer.insetT = t;
        layer.insetR = r;
        layer.insetB = b;
        updateLayerBounds(getBounds());
    }

    public void setLayerSize(int index, int w, int h) {
        Layer layer = mLayers.get(index);
        layer.width = w;
        layer.height = h;
        updateLayerBounds(getBounds());
    }

    public void setLayerGravity(int index, int gravity) {
        mLayers.get(index).gravity = gravity;
        updateLayerBounds(getBounds());
    }

    public void setPaddingMode(int mode) {
        mPaddingMode = mode;
    }

    public int getPaddingMode() {
        return mPaddingMode;
    }

    @Override
    public void draw(Canvas canvas) {
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.draw(canvas);
        }
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        updateLayerBounds(bounds);
    }

    void updateLayerBounds(Rect bounds) {
        int padL = 0, padT = 0, padR = 0, padB = 0;
        Rect pad = mTmp;
        for (Layer l : mLayers) {
            if (l.drawable == null) continue;
            Rect container = new Rect(bounds.left + l.insetL + padL, bounds.top + l.insetT + padT,
                    bounds.right - l.insetR - padR, bounds.bottom - l.insetB - padB);
            if (l.width >= 0 || l.height >= 0 || l.gravity != Gravity.NO_GRAVITY) {
                int w = l.width >= 0 ? l.width : l.drawable.getIntrinsicWidth();
                int h = l.height >= 0 ? l.height : l.drawable.getIntrinsicHeight();
                int gravity = l.gravity;
                if (gravity == Gravity.NO_GRAVITY) gravity = Gravity.FILL;
                if (w < 0) gravity |= Gravity.FILL_HORIZONTAL;
                if (h < 0) gravity |= Gravity.FILL_VERTICAL;
                Rect out = new Rect();
                Gravity.apply(gravity, Math.max(0, w), Math.max(0, h), container, out, getLayoutDirection());
                l.drawable.setBounds(out);
            } else {
                l.drawable.setBounds(container);
            }
            if (mPaddingMode == PADDING_MODE_NEST && l.drawable.getPadding(pad)) {
                padL += pad.left;
                padT += pad.top;
                padR += pad.right;
                padB += pad.bottom;
            }
        }
    }

    @Override
    public boolean getPadding(Rect padding) {
        padding.set(0, 0, 0, 0);
        Rect pad = new Rect();
        for (Layer l : mLayers) {
            if (l.drawable == null || !l.drawable.getPadding(pad)) continue;
            if (mPaddingMode == PADDING_MODE_NEST) {
                padding.left += pad.left;
                padding.top += pad.top;
                padding.right += pad.right;
                padding.bottom += pad.bottom;
            } else {
                padding.left = Math.max(padding.left, pad.left);
                padding.top = Math.max(padding.top, pad.top);
                padding.right = Math.max(padding.right, pad.right);
                padding.bottom = Math.max(padding.bottom, pad.bottom);
            }
        }
        return (padding.left | padding.top | padding.right | padding.bottom) != 0;
    }

    @Override
    public int getIntrinsicWidth() {
        int width = -1;
        for (Layer l : mLayers) {
            if (l.drawable == null) continue;
            int w = l.width >= 0 ? l.width : l.drawable.getIntrinsicWidth();
            if (w >= 0) width = Math.max(width, w + l.insetL + l.insetR);
        }
        return width;
    }

    @Override
    public int getIntrinsicHeight() {
        int height = -1;
        for (Layer l : mLayers) {
            if (l.drawable == null) continue;
            int h = l.height >= 0 ? l.height : l.drawable.getIntrinsicHeight();
            if (h >= 0) height = Math.max(height, h + l.insetT + l.insetB);
        }
        return height;
    }

    @Override
    public boolean isStateful() {
        for (Layer l : mLayers) {
            if (l.drawable != null && l.drawable.isStateful()) return true;
        }
        return super.isStateful();
    }

    @Override
    protected boolean onStateChange(int[] state) {
        boolean changed = false;
        for (Layer l : mLayers) {
            if (l.drawable != null && l.drawable.setState(state)) changed = true;
        }
        if (changed) updateLayerBounds(getBounds());
        return changed;
    }

    @Override
    protected boolean onLevelChange(int level) {
        boolean changed = false;
        for (Layer l : mLayers) {
            if (l.drawable != null && l.drawable.setLevel(level)) changed = true;
        }
        return changed;
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = super.setVisible(visible, restart);
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.setVisible(visible, restart);
        }
        return changed;
    }

    @Override
    public void setAlpha(int alpha) {
        mAlpha = alpha;
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.setAlpha(alpha);
        }
    }

    @Override
    public int getAlpha() {
        return mAlpha;
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.setColorFilter(colorFilter);
        }
    }

    @Override
    public void setTintList(android.content.res.ColorStateList tint) {
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.setTintList(tint);
        }
    }

    @Override
    public int getOpacity() {
        int op = PixelFormat.TRANSPARENT;
        for (int i = 0; i < mLayers.size(); i++) {
            Drawable d = mLayers.get(i).drawable;
            if (d != null) op = i == 0 ? d.getOpacity() : resolveOpacity(op, d.getOpacity());
        }
        return op;
    }

    @Override
    public void jumpToCurrentState() {
        for (Layer l : mLayers) {
            if (l.drawable != null) l.drawable.jumpToCurrentState();
        }
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

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.LayerDrawable);
        mPaddingMode = a.getInt(android.R.styleable.LayerDrawable_paddingMode, mPaddingMode);
        a.recycle();
        inflateLayers(r, parser, attrs, theme);
        updateLayerBounds(getBounds());
    }

    void inflateLayers(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth || !parser.getName().equals("item")) continue;
            final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.LayerDrawableItem);
            Layer layer = new Layer();
            layer.id = a.getResourceId(android.R.styleable.LayerDrawableItem_id, -1);
            int inset = 0;
            layer.insetL = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_left, inset);
            layer.insetT = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_top, inset);
            layer.insetR = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_right, inset);
            layer.insetB = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_bottom, inset);
            layer.insetL = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_start, layer.insetL);
            layer.insetR = a.getDimensionPixelOffset(android.R.styleable.LayerDrawableItem_end, layer.insetR);
            layer.width = a.getDimensionPixelSize(android.R.styleable.LayerDrawableItem_width, -1);
            layer.height = a.getDimensionPixelSize(android.R.styleable.LayerDrawableItem_height, -1);
            layer.gravity = a.getInt(android.R.styleable.LayerDrawableItem_gravity, Gravity.NO_GRAVITY);
            Drawable dr = a.getDrawable(android.R.styleable.LayerDrawableItem_drawable);
            a.recycle();
            if (dr == null) dr = inflateChild(r, parser, attrs, theme);
            layer.drawable = dr;
            dr.setCallback(this);
            mLayers.add(layer);
        }
    }
}