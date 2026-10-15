package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.View;

/** Holds its drawable; images are not drawn by this runtime's 2D views. */
public class ImageView extends View {
    public enum ScaleType {
        MATRIX, FIT_XY, FIT_START, FIT_CENTER, FIT_END, CENTER, CENTER_CROP, CENTER_INSIDE
    }

    private Drawable mDrawable;
    private ScaleType mScaleType = ScaleType.FIT_CENTER;

    public ImageView(Context context) {
        super(context);
    }

    public ImageView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ImageView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public Drawable getDrawable() {
        return mDrawable;
    }

    public void setImageDrawable(Drawable drawable) {
        mDrawable = drawable;
        invalidate();
    }

    public void setImageResource(int resId) {
        Drawable d = null;
        try {
            d = getContext().getResources().getDrawable(resId);
        } catch (RuntimeException e) {
            android.util.Log.w("ImageView", "Unable to find resource: " + resId);
        }
        setImageDrawable(d);
    }

    public void setScaleType(ScaleType scaleType) {
        mScaleType = scaleType;
    }

    public ScaleType getScaleType() {
        return mScaleType;
    }

    public void setAdjustViewBounds(boolean adjustViewBounds) {}
}
