package android.graphics;

/** A rectangular region (complex regions are reduced to their bounds). */
public class Region {
    public enum Op {
        DIFFERENCE, INTERSECT, UNION, XOR, REVERSE_DIFFERENCE, REPLACE
    }

    private final Rect mBounds = new Rect();

    public Region() {}

    public Region(Rect r) {
        mBounds.set(r);
    }

    public Region(int left, int top, int right, int bottom) {
        mBounds.set(left, top, right, bottom);
    }

    public void setEmpty() {
        mBounds.setEmpty();
    }

    public boolean set(Rect r) {
        mBounds.set(r);
        return !mBounds.isEmpty();
    }

    public boolean set(int left, int top, int right, int bottom) {
        mBounds.set(left, top, right, bottom);
        return !mBounds.isEmpty();
    }

    public boolean isEmpty() {
        return mBounds.isEmpty();
    }

    public boolean isRect() {
        return true;
    }

    public Rect getBounds() {
        return new Rect(mBounds);
    }

    public boolean getBounds(Rect r) {
        r.set(mBounds);
        return !mBounds.isEmpty();
    }

    public boolean contains(int x, int y) {
        return mBounds.contains(x, y);
    }

    public boolean op(Rect r, Op op) {
        switch (op) {
            case INTERSECT:
                if (!mBounds.intersect(r)) mBounds.setEmpty();
                break;
            case UNION:
                mBounds.union(r);
                break;
            case REPLACE:
                mBounds.set(r);
                break;
            default:
                break;
        }
        return !mBounds.isEmpty();
    }

    public boolean union(Rect r) {
        return op(r, Op.UNION);
    }
}