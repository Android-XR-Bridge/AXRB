package android.graphics;

public class PointF {
    public float x;
    public float y;

    public PointF() {}

    public PointF(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public PointF(Point p) {
        this.x = p.x;
        this.y = p.y;
    }

    public final void set(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public final void set(PointF p) {
        this.x = p.x;
        this.y = p.y;
    }

    public final void negate() {
        x = -x;
        y = -y;
    }

    public final void offset(float dx, float dy) {
        x += dx;
        y += dy;
    }

    public final boolean equals(float x, float y) {
        return this.x == x && this.y == y;
    }

    public final float length() {
        return (float) Math.hypot(x, y);
    }

    public static float length(float x, float y) {
        return (float) Math.hypot(x, y);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof PointF && ((PointF) o).x == x && ((PointF) o).y == y;
    }

    @Override
    public int hashCode() {
        return Float.floatToIntBits(x) * 31 + Float.floatToIntBits(y);
    }

    @Override
    public String toString() {
        return "PointF(" + x + ", " + y + ")";
    }
}