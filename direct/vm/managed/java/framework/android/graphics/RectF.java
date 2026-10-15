package android.graphics;

public class RectF {
    public float left;
    public float top;
    public float right;
    public float bottom;

    public RectF() {}

    public RectF(float left, float top, float right, float bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public RectF(RectF r) {
        if (r != null) set(r.left, r.top, r.right, r.bottom);
    }

    public RectF(Rect r) {
        if (r != null) set(r.left, r.top, r.right, r.bottom);
    }

    public void set(float left, float top, float right, float bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public void set(RectF src) {
        set(src.left, src.top, src.right, src.bottom);
    }

    public void set(Rect src) {
        set(src.left, src.top, src.right, src.bottom);
    }

    public final boolean isEmpty() {
        return left >= right || top >= bottom;
    }

    public final float width() {
        return right - left;
    }

    public final float height() {
        return bottom - top;
    }

    public final float centerX() {
        return (left + right) * 0.5f;
    }

    public final float centerY() {
        return (top + bottom) * 0.5f;
    }

    public void offset(float dx, float dy) {
        left += dx;
        top += dy;
        right += dx;
        bottom += dy;
    }

    public void inset(float dx, float dy) {
        left += dx;
        top += dy;
        right -= dx;
        bottom -= dy;
    }

    public boolean contains(float x, float y) {
        return left < right && top < bottom && x >= left && x < right && y >= top && y < bottom;
    }

    public boolean contains(float left, float top, float right, float bottom) {
        return this.left < this.right && this.top < this.bottom && this.left <= left && this.top <= top
                && this.right >= right && this.bottom >= bottom;
    }

    public boolean contains(RectF r) {
        return contains(r.left, r.top, r.right, r.bottom);
    }

    public void setEmpty() {
        left = right = top = bottom = 0;
    }

    public void offsetTo(float newLeft, float newTop) {
        right += newLeft - left;
        bottom += newTop - top;
        left = newLeft;
        top = newTop;
    }

    public boolean intersect(float left, float top, float right, float bottom) {
        if (this.left < right && left < this.right && this.top < bottom && top < this.bottom) {
            if (this.left < left) this.left = left;
            if (this.top < top) this.top = top;
            if (this.right > right) this.right = right;
            if (this.bottom > bottom) this.bottom = bottom;
            return true;
        }
        return false;
    }

    public boolean intersect(RectF r) {
        return intersect(r.left, r.top, r.right, r.bottom);
    }

    public boolean intersects(float left, float top, float right, float bottom) {
        return this.left < right && left < this.right && this.top < bottom && top < this.bottom;
    }

    public static boolean intersects(RectF a, RectF b) {
        return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
    }

    public boolean setIntersect(RectF a, RectF b) {
        if (a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom) {
            left = Math.max(a.left, b.left);
            top = Math.max(a.top, b.top);
            right = Math.min(a.right, b.right);
            bottom = Math.min(a.bottom, b.bottom);
            return true;
        }
        return false;
    }

    public void union(float left, float top, float right, float bottom) {
        if ((left < right) && (top < bottom)) {
            if ((this.left < this.right) && (this.top < this.bottom)) {
                if (this.left > left) this.left = left;
                if (this.top > top) this.top = top;
                if (this.right < right) this.right = right;
                if (this.bottom < bottom) this.bottom = bottom;
            } else {
                this.left = left;
                this.top = top;
                this.right = right;
                this.bottom = bottom;
            }
        }
    }

    public void union(RectF r) {
        union(r.left, r.top, r.right, r.bottom);
    }

    public void union(float x, float y) {
        if (x < left) {
            left = x;
        } else if (x > right) {
            right = x;
        }
        if (y < top) {
            top = y;
        } else if (y > bottom) {
            bottom = y;
        }
    }

    public void sort() {
        if (left > right) {
            float temp = left;
            left = right;
            right = temp;
        }
        if (top > bottom) {
            float temp = top;
            top = bottom;
            bottom = temp;
        }
    }

    public void round(Rect dst) {
        dst.set(Math.round(left), Math.round(top), Math.round(right), Math.round(bottom));
    }

    public void roundOut(Rect dst) {
        dst.set((int) Math.floor(left), (int) Math.floor(top), (int) Math.ceil(right), (int) Math.ceil(bottom));
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof RectF)) return false;
        RectF r = (RectF) o;
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }

    @Override
    public int hashCode() {
        return Float.floatToIntBits(left) * 31 + Float.floatToIntBits(top) * 17 + Float.floatToIntBits(right) * 7
                + Float.floatToIntBits(bottom);
    }

    @Override
    public String toString() {
        return "RectF(" + left + ", " + top + ", " + right + ", " + bottom + ")";
    }
}
