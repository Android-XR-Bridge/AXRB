package android.graphics;

public final class Insets {
    public static final Insets NONE = new Insets(0, 0, 0, 0);

    public final int left;
    public final int top;
    public final int right;
    public final int bottom;

    private Insets(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public static Insets of(int left, int top, int right, int bottom) {
        if (left == 0 && top == 0 && right == 0 && bottom == 0) return NONE;
        return new Insets(left, top, right, bottom);
    }

    public static Insets of(Rect r) {
        return r == null ? NONE : of(r.left, r.top, r.right, r.bottom);
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Insets)) return false;
        Insets i = (Insets) o;
        return left == i.left && top == i.top && right == i.right && bottom == i.bottom;
    }

    @Override
    public int hashCode() {
        return ((left * 31 + top) * 31 + right) * 31 + bottom;
    }

    @Override
    public String toString() {
        return "Insets{left=" + left + ", top=" + top + ", right=" + right + ", bottom=" + bottom + '}';
    }
}