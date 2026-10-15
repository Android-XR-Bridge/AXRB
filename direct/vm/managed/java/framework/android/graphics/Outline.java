package android.graphics;

/** The shape of a view for shadows and clipping (the runner draws no shadows). */
public final class Outline {
    public Rect mRect;
    public float mRadius;
    public float mAlpha;
    public Path mPath;
    private boolean mEmpty = true;

    public Outline() {}

    public Outline(Outline src) {
        set(src);
    }

    public void setEmpty() {
        mEmpty = true;
        mRect = null;
        mPath = null;
        mRadius = 0;
    }

    public boolean isEmpty() {
        return mEmpty;
    }

    public boolean canClip() {
        return mPath == null;
    }

    public void setAlpha(float alpha) {
        mAlpha = alpha;
    }

    public float getAlpha() {
        return mAlpha;
    }

    public void set(Outline src) {
        mEmpty = src.mEmpty;
        mRect = src.mRect != null ? new Rect(src.mRect) : null;
        mRadius = src.mRadius;
        mAlpha = src.mAlpha;
        mPath = src.mPath;
    }

    public void setRect(int left, int top, int right, int bottom) {
        setRoundRect(left, top, right, bottom, 0.0f);
    }

    public void setRect(Rect rect) {
        setRect(rect.left, rect.top, rect.right, rect.bottom);
    }

    public void setRoundRect(int left, int top, int right, int bottom, float radius) {
        if (left >= right || top >= bottom) {
            setEmpty();
            return;
        }
        mRect = new Rect(left, top, right, bottom);
        mRadius = radius;
        mPath = null;
        mEmpty = false;
    }

    public void setRoundRect(Rect rect, float radius) {
        setRoundRect(rect.left, rect.top, rect.right, rect.bottom, radius);
    }

    public boolean getRect(Rect outRect) {
        if (mRect == null) return false;
        outRect.set(mRect);
        return true;
    }

    public float getRadius() {
        return mRadius;
    }

    public void setOval(int left, int top, int right, int bottom) {
        if (left >= right || top >= bottom) {
            setEmpty();
            return;
        }
        if ((bottom - top) == (right - left)) {
            setRoundRect(left, top, right, bottom, (bottom - top) / 2.0f);
            return;
        }
        mPath = new Path();
        mPath.addOval(new RectF(left, top, right, bottom), Path.Direction.CW);
        mRect = null;
        mEmpty = false;
    }

    public void setOval(Rect rect) {
        setOval(rect.left, rect.top, rect.right, rect.bottom);
    }

    public void setConvexPath(Path convexPath) {
        setPath(convexPath);
    }

    public void setPath(Path path) {
        mPath = new Path(path);
        mRect = null;
        mEmpty = false;
    }

    public void offset(int dx, int dy) {
        if (mRect != null) mRect.offset(dx, dy);
        if (mPath != null) mPath.offset(dx, dy);
    }
}
