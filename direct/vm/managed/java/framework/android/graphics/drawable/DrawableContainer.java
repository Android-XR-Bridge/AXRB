package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import java.util.ArrayList;

/** Shows one child drawable at a time (state lists, level lists, animations). */
public class DrawableContainer extends Drawable implements Drawable.Callback {
    final ArrayList<Drawable> mChildren = new ArrayList<Drawable>();
    private int mCurIndex = -1;
    private int mAlpha = 0xFF;
    private ColorFilter mColorFilter;
    boolean mConstantSize;
    boolean mVariablePadding;

    int addChild(Drawable dr) {
        mChildren.add(dr);
        dr.setVisible(false, true);
        dr.setCallback(this);
        return mChildren.size() - 1;
    }

    public int getCurrentIndex() {
        return mCurIndex;
    }

    public boolean selectDrawable(int index) {
        if (index == mCurIndex) return false;
        if (mCurIndex >= 0 && mCurIndex < mChildren.size()) mChildren.get(mCurIndex).setVisible(false, false);
        if (index >= 0 && index < mChildren.size()) {
            Drawable d = mChildren.get(index);
            mCurIndex = index;
            d.setVisible(isVisible(), true);
            d.setAlpha(mAlpha);
            d.setColorFilter(mColorFilter);
            d.setState(getState());
            d.setLevel(getLevel());
            d.setBounds(getBounds());
            d.setLayoutDirection(getLayoutDirection());
        } else {
            mCurIndex = -1;
        }
        invalidateSelf();
        return true;
    }

    Drawable current() {
        return mCurIndex >= 0 && mCurIndex < mChildren.size() ? mChildren.get(mCurIndex) : null;
    }

    @Override
    public Drawable getCurrent() {
        Drawable d = current();
        return d != null ? d : this;
    }

    @Override
    public void draw(Canvas canvas) {
        Drawable d = current();
        if (d != null) d.draw(canvas);
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        Drawable d = current();
        if (d != null) d.setBounds(bounds);
    }

    @Override
    public boolean isStateful() {
        for (Drawable d : mChildren) {
            if (d.isStateful()) return true;
        }
        return false;
    }

    @Override
    protected boolean onStateChange(int[] state) {
        Drawable d = current();
        return d != null && d.setState(state);
    }

    @Override
    protected boolean onLevelChange(int level) {
        Drawable d = current();
        return d != null && d.setLevel(level);
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = super.setVisible(visible, restart);
        Drawable d = current();
        if (d != null) d.setVisible(visible, restart);
        return changed;
    }

    @Override
    public boolean getPadding(Rect padding) {
        if (mVariablePadding || mChildren.isEmpty()) {
            Drawable d = current();
            if (d != null) return d.getPadding(padding);
            return super.getPadding(padding);
        }
        Rect r = new Rect();
        boolean any = false;
        padding.set(0, 0, 0, 0);
        for (Drawable d : mChildren) {
            if (d.getPadding(r)) {
                any = true;
                padding.left = Math.max(padding.left, r.left);
                padding.top = Math.max(padding.top, r.top);
                padding.right = Math.max(padding.right, r.right);
                padding.bottom = Math.max(padding.bottom, r.bottom);
            }
        }
        return any;
    }

    @Override
    public int getIntrinsicWidth() {
        if (mConstantSize) {
            int w = -1;
            for (Drawable d : mChildren) w = Math.max(w, d.getIntrinsicWidth());
            return w;
        }
        Drawable d = current();
        return d != null ? d.getIntrinsicWidth() : -1;
    }

    @Override
    public int getIntrinsicHeight() {
        if (mConstantSize) {
            int h = -1;
            for (Drawable d : mChildren) h = Math.max(h, d.getIntrinsicHeight());
            return h;
        }
        Drawable d = current();
        return d != null ? d.getIntrinsicHeight() : -1;
    }

    @Override
    public int getMinimumWidth() {
        Drawable d = current();
        return d != null ? d.getMinimumWidth() : 0;
    }

    @Override
    public int getMinimumHeight() {
        Drawable d = current();
        return d != null ? d.getMinimumHeight() : 0;
    }

    @Override
    public void setAlpha(int alpha) {
        mAlpha = alpha;
        Drawable d = current();
        if (d != null) d.setAlpha(alpha);
    }

    @Override
    public int getAlpha() {
        return mAlpha;
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
        Drawable d = current();
        if (d != null) d.setColorFilter(colorFilter);
    }

    @Override
    public void setTintList(android.content.res.ColorStateList tint) {
        for (Drawable d : mChildren) d.setTintList(tint);
    }

    @Override
    public int getOpacity() {
        Drawable d = current();
        return d != null && d.isVisible() ? d.getOpacity() : PixelFormat.TRANSPARENT;
    }

    @Override
    public void jumpToCurrentState() {
        Drawable d = current();
        if (d != null) d.jumpToCurrentState();
    }

    @Override
    public void invalidateDrawable(Drawable who) {
        if (who == current()) invalidateSelf();
    }

    @Override
    public void scheduleDrawable(Drawable who, Runnable what, long when) {
        if (who == current()) scheduleSelf(what, when);
    }

    @Override
    public void unscheduleDrawable(Drawable who, Runnable what) {
        if (who == current()) unscheduleSelf(what);
    }

    @Override
    public boolean onLayoutDirectionChanged(int layoutDirection) {
        for (Drawable d : mChildren) d.setLayoutDirection(layoutDirection);
        return false;
    }
}