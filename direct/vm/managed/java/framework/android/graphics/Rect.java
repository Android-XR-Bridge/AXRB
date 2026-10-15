package android.graphics;

public final class Rect {
    public int left;
    public int top;
    public int right;
    public int bottom;

    public Rect() {}

    public Rect(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public Rect(Rect r) {
        if (r != null) set(r);
    }

    public void set(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public void set(Rect src) {
        set(src.left, src.top, src.right, src.bottom);
    }

    public void setEmpty() {
        left = right = top = bottom = 0;
    }

    public boolean isEmpty() {
        return left >= right || top >= bottom;
    }

    public int width() {
        return right - left;
    }

    public int height() {
        return bottom - top;
    }

    public int centerX() {
        return (left + right) >> 1;
    }

    public int centerY() {
        return (top + bottom) >> 1;
    }

    public float exactCenterX() {
        return (left + right) * 0.5f;
    }

    public float exactCenterY() {
        return (top + bottom) * 0.5f;
    }

    public void inset(int left, int top, int right, int bottom) {
        this.left += left;
        this.top += top;
        this.right -= right;
        this.bottom -= bottom;
    }

    public boolean setIntersect(Rect a, Rect b) {
        if (a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom) {
            left = Math.max(a.left, b.left);
            top = Math.max(a.top, b.top);
            right = Math.min(a.right, b.right);
            bottom = Math.min(a.bottom, b.bottom);
            return true;
        }
        return false;
    }

    public void sort() {
        if (left > right) {
            int t = left;
            left = right;
            right = t;
        }
        if (top > bottom) {
            int t = top;
            top = bottom;
            bottom = t;
        }
    }

    public void scale(float scale) {
        if (scale != 1.0f) {
            left = (int) (left * scale + 0.5f);
            top = (int) (top * scale + 0.5f);
            right = (int) (right * scale + 0.5f);
            bottom = (int) (bottom * scale + 0.5f);
        }
    }

    public void offset(int dx, int dy) {
        left += dx;
        top += dy;
        right += dx;
        bottom += dy;
    }

    public void offsetTo(int newLeft, int newTop) {
        right += newLeft - left;
        bottom += newTop - top;
        left = newLeft;
        top = newTop;
    }

    public void inset(int dx, int dy) {
        left += dx;
        top += dy;
        right -= dx;
        bottom -= dy;
    }

    public boolean contains(int x, int y) {
        return left < right && top < bottom && x >= left && x < right && y >= top && y < bottom;
    }

    public boolean contains(Rect r) {
        return left < right && top < bottom && left <= r.left && top <= r.top && right >= r.right && bottom >= r.bottom;
    }

    public boolean intersect(int l, int t, int r, int b) {
        if (left < r && l < right && top < b && t < bottom) {
            if (left < l) left = l;
            if (top < t) top = t;
            if (right > r) right = r;
            if (bottom > b) bottom = b;
            return true;
        }
        return false;
    }

    public boolean intersect(Rect r) {
        return intersect(r.left, r.top, r.right, r.bottom);
    }

    public boolean intersects(int l, int t, int r, int b) {
        return left < r && l < right && top < b && t < bottom;
    }

    public void union(int l, int t, int r, int b) {
        if (l >= r || t >= b) return;
        if (left < right && top < bottom) {
            if (left > l) left = l;
            if (top > t) top = t;
            if (right < r) right = r;
            if (bottom < b) bottom = b;
        } else {
            set(l, t, r, b);
        }
    }

    public void union(Rect r) {
        union(r.left, r.top, r.right, r.bottom);
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Rect)) return false;
        Rect r = (Rect) o;
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }

    @Override
    public int hashCode() {
        return 31 * (31 * (31 * left + top) + right) + bottom;
    }

    @Override
    public String toString() {
        return "Rect(" + left + ", " + top + " - " + right + ", " + bottom + ")";
    }

    public String toShortString() {
        return "[" + left + "," + top + "][" + right + "," + bottom + "]";
    }
}
