package android.graphics;

/**
 * A geometric path of lines, quadratic and cubic Béziers. Arcs, ovals, circles
 * and round rects are converted to cubic Béziers when added. The renderer
 * receives the op list as-is.
 */
public class Path {
    static final int VERB_MOVE = 0;
    static final int VERB_LINE = 1;
    static final int VERB_QUAD = 2;
    static final int VERB_CUBIC = 3;
    static final int VERB_CLOSE = 4;

    public enum Direction {
        CW, CCW
    }

    public enum FillType {
        WINDING, EVEN_ODD, INVERSE_WINDING, INVERSE_EVEN_ODD
    }

    public enum Op {
        DIFFERENCE, INTERSECT, UNION, XOR, REVERSE_DIFFERENCE
    }

    float[] mOps = new float[32];
    int mCount;
    private FillType mFillType = FillType.WINDING;
    private float mLastX, mLastY;
    private float mStartX, mStartY;
    private boolean mHasMove;

    public Path() {}

    public Path(Path src) {
        if (src != null) set(src);
    }

    public void reset() {
        mCount = 0;
        mHasMove = false;
        mLastX = mLastY = mStartX = mStartY = 0;
    }

    public void rewind() {
        reset();
    }

    public void set(Path src) {
        if (src == this) return;
        mOps = src.mOps.clone();
        mCount = src.mCount;
        mFillType = src.mFillType;
        mLastX = src.mLastX;
        mLastY = src.mLastY;
        mStartX = src.mStartX;
        mStartY = src.mStartY;
        mHasMove = src.mHasMove;
    }

    public FillType getFillType() {
        return mFillType;
    }

    public void setFillType(FillType ft) {
        mFillType = ft;
    }

    public boolean isInverseFillType() {
        return mFillType == FillType.INVERSE_WINDING || mFillType == FillType.INVERSE_EVEN_ODD;
    }

    public void toggleInverseFillType() {
        switch (mFillType) {
            case WINDING: mFillType = FillType.INVERSE_WINDING; break;
            case EVEN_ODD: mFillType = FillType.INVERSE_EVEN_ODD; break;
            case INVERSE_WINDING: mFillType = FillType.WINDING; break;
            default: mFillType = FillType.EVEN_ODD; break;
        }
    }

    public boolean isEmpty() {
        return mCount == 0;
    }

    public boolean isConvex() {
        return false;
    }

    private void push(float v) {
        if (mCount == mOps.length) {
            float[] next = new float[mOps.length * 2];
            System.arraycopy(mOps, 0, next, 0, mCount);
            mOps = next;
        }
        mOps[mCount++] = v;
    }

    private void ensureMove() {
        if (!mHasMove) moveTo(mLastX, mLastY);
    }

    public void moveTo(float x, float y) {
        push(VERB_MOVE);
        push(x);
        push(y);
        mLastX = mStartX = x;
        mLastY = mStartY = y;
        mHasMove = true;
    }

    public void rMoveTo(float dx, float dy) {
        moveTo(mLastX + dx, mLastY + dy);
    }

    public void lineTo(float x, float y) {
        ensureMove();
        push(VERB_LINE);
        push(x);
        push(y);
        mLastX = x;
        mLastY = y;
    }

    public void rLineTo(float dx, float dy) {
        lineTo(mLastX + dx, mLastY + dy);
    }

    public void quadTo(float x1, float y1, float x2, float y2) {
        ensureMove();
        push(VERB_QUAD);
        push(x1);
        push(y1);
        push(x2);
        push(y2);
        mLastX = x2;
        mLastY = y2;
    }

    public void rQuadTo(float dx1, float dy1, float dx2, float dy2) {
        quadTo(mLastX + dx1, mLastY + dy1, mLastX + dx2, mLastY + dy2);
    }

    public void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        ensureMove();
        push(VERB_CUBIC);
        push(x1);
        push(y1);
        push(x2);
        push(y2);
        push(x3);
        push(y3);
        mLastX = x3;
        mLastY = y3;
    }

    public void rCubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        cubicTo(mLastX + x1, mLastY + y1, mLastX + x2, mLastY + y2, mLastX + x3, mLastY + y3);
    }

    public void close() {
        if (mCount == 0) return;
        push(VERB_CLOSE);
        mLastX = mStartX;
        mLastY = mStartY;
        mHasMove = false;
    }

    public void setLastPoint(float dx, float dy) {
        if (mCount >= 2) {
            mOps[mCount - 2] = dx;
            mOps[mCount - 1] = dy;
            mLastX = dx;
            mLastY = dy;
        } else {
            moveTo(dx, dy);
        }
    }

    // --- arcs and shapes ------------------------------------------------------

    /** Appends an elliptical arc as cubics; `forceMoveTo` starts a new contour. */
    private void arc(RectF oval, float startAngle, float sweepAngle, boolean moveFirst, boolean forceMoveTo) {
        float cx = oval.centerX(), cy = oval.centerY();
        float rx = oval.width() / 2, ry = oval.height() / 2;
        double start = Math.toRadians(startAngle);
        float sx = cx + rx * (float) Math.cos(start);
        float sy = cy + ry * (float) Math.sin(start);
        if (moveFirst || forceMoveTo || !mHasMove) {
            moveTo(sx, sy);
        } else {
            lineTo(sx, sy);
        }
        if (sweepAngle == 0) return;
        int segments = (int) Math.ceil(Math.abs(sweepAngle) / 90.0);
        double step = Math.toRadians(sweepAngle) / segments;
        double k = 4.0 / 3.0 * Math.tan(step / 4);
        double a = start;
        for (int i = 0; i < segments; i++) {
            double b = a + step;
            double cosA = Math.cos(a), sinA = Math.sin(a), cosB = Math.cos(b), sinB = Math.sin(b);
            cubicTo((float) (cx + rx * (cosA - k * sinA)), (float) (cy + ry * (sinA + k * cosA)),
                    (float) (cx + rx * (cosB + k * sinB)), (float) (cy + ry * (sinB - k * cosB)),
                    (float) (cx + rx * cosB), (float) (cy + ry * sinB));
            a = b;
        }
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle, boolean forceMoveTo) {
        arc(oval, startAngle, sweepAngle, false, forceMoveTo);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle) {
        arcTo(oval, startAngle, sweepAngle, false);
    }

    public void arcTo(float left, float top, float right, float bottom, float startAngle, float sweepAngle,
            boolean forceMoveTo) {
        arcTo(new RectF(left, top, right, bottom), startAngle, sweepAngle, forceMoveTo);
    }

    public void addArc(RectF oval, float startAngle, float sweepAngle) {
        arc(oval, startAngle, sweepAngle, true, true);
    }

    public void addArc(float left, float top, float right, float bottom, float startAngle, float sweepAngle) {
        addArc(new RectF(left, top, right, bottom), startAngle, sweepAngle);
    }

    public void addRect(RectF rect, Direction dir) {
        addRect(rect.left, rect.top, rect.right, rect.bottom, dir);
    }

    public void addRect(float left, float top, float right, float bottom, Direction dir) {
        moveTo(left, top);
        if (dir == Direction.CCW) {
            lineTo(left, bottom);
            lineTo(right, bottom);
            lineTo(right, top);
        } else {
            lineTo(right, top);
            lineTo(right, bottom);
            lineTo(left, bottom);
        }
        close();
    }

    public void addOval(RectF oval, Direction dir) {
        addOval(oval.left, oval.top, oval.right, oval.bottom, dir);
    }

    public void addOval(float left, float top, float right, float bottom, Direction dir) {
        arc(new RectF(left, top, right, bottom), 0, dir == Direction.CCW ? -360 : 360, true, true);
        close();
    }

    public void addCircle(float x, float y, float radius, Direction dir) {
        addOval(x - radius, y - radius, x + radius, y + radius, dir);
    }

    public void addRoundRect(RectF rect, float rx, float ry, Direction dir) {
        addRoundRect(rect.left, rect.top, rect.right, rect.bottom, rx, ry, dir);
    }

    public void addRoundRect(float left, float top, float right, float bottom, float rx, float ry, Direction dir) {
        addRoundRect(left, top, right, bottom, new float[] {rx, ry, rx, ry, rx, ry, rx, ry}, dir);
    }

    public void addRoundRect(RectF rect, float[] radii, Direction dir) {
        addRoundRect(rect.left, rect.top, rect.right, rect.bottom, radii, dir);
    }

    /** radii: {tlx, tly, trx, try, brx, bry, blx, bly}. */
    public void addRoundRect(float left, float top, float right, float bottom, float[] radii, Direction dir) {
        if (radii.length < 8) throw new ArrayIndexOutOfBoundsException("radii[] needs 8 values");
        float w = right - left, h = bottom - top;
        float[] r = new float[8];
        for (int i = 0; i < 8; i++) r[i] = Math.max(0, radii[i]);
        float scale = 1;
        scale = Math.min(scale, w / Math.max(1e-6f, r[0] + r[2]));
        scale = Math.min(scale, w / Math.max(1e-6f, r[6] + r[4]));
        scale = Math.min(scale, h / Math.max(1e-6f, r[1] + r[7]));
        scale = Math.min(scale, h / Math.max(1e-6f, r[3] + r[5]));
        if (scale < 1) {
            for (int i = 0; i < 8; i++) r[i] *= scale;
        }
        // Clockwise from the top-left corner's end.
        moveTo(left + r[0], top);
        lineTo(right - r[2], top);
        if (r[2] > 0 || r[3] > 0) arc(new RectF(right - 2 * r[2], top, right, top + 2 * r[3]), 270, 90, false, false);
        lineTo(right, bottom - r[5]);
        if (r[4] > 0 || r[5] > 0) arc(new RectF(right - 2 * r[4], bottom - 2 * r[5], right, bottom), 0, 90, false, false);
        lineTo(left + r[6], bottom);
        if (r[6] > 0 || r[7] > 0) arc(new RectF(left, bottom - 2 * r[7], left + 2 * r[6], bottom), 90, 90, false, false);
        lineTo(left, top + r[1]);
        if (r[0] > 0 || r[1] > 0) arc(new RectF(left, top, left + 2 * r[0], top + 2 * r[1]), 180, 90, false, false);
        close();
    }

    public void addPath(Path src) {
        addPath(src, null);
    }

    public void addPath(Path src, float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        addPath(src, m);
    }

    public void addPath(Path src, Matrix matrix) {
        float[] ops = src.mOps;
        int n = src.mCount;
        float[] pts = new float[6];
        int i = 0;
        while (i < n) {
            int verb = (int) ops[i++];
            int coords = verb == VERB_MOVE || verb == VERB_LINE ? 2 : verb == VERB_QUAD ? 4 : verb == VERB_CUBIC ? 6 : 0;
            System.arraycopy(ops, i, pts, 0, coords);
            if (matrix != null && coords > 0) matrix.mapPoints(pts, 0, pts, 0, coords / 2);
            i += coords;
            switch (verb) {
                case VERB_MOVE: moveTo(pts[0], pts[1]); break;
                case VERB_LINE: lineTo(pts[0], pts[1]); break;
                case VERB_QUAD: quadTo(pts[0], pts[1], pts[2], pts[3]); break;
                case VERB_CUBIC: cubicTo(pts[0], pts[1], pts[2], pts[3], pts[4], pts[5]); break;
                default: close(); break;
            }
        }
    }

    public void offset(float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        transform(m);
    }

    public void offset(float dx, float dy, Path dst) {
        if (dst != null) {
            dst.set(this);
            dst.offset(dx, dy);
        } else {
            offset(dx, dy);
        }
    }

    public void transform(Matrix matrix, Path dst) {
        Path out = new Path();
        out.mFillType = mFillType;
        out.addPath(this, matrix);
        if (dst != null) dst.set(out); else set(out);
    }

    public void transform(Matrix matrix) {
        transform(matrix, null);
    }

    public boolean op(Path path, Op op) {
        return false;  // boolean path ops are not implemented
    }

    public void computeBounds(RectF bounds, boolean exact) {
        if (mCount == 0) {
            bounds.set(0, 0, 0, 0);
            return;
        }
        float l = Float.MAX_VALUE, t = Float.MAX_VALUE, r = -Float.MAX_VALUE, b = -Float.MAX_VALUE;
        int i = 0;
        while (i < mCount) {
            int verb = (int) mOps[i++];
            int coords = verb == VERB_MOVE || verb == VERB_LINE ? 2 : verb == VERB_QUAD ? 4 : verb == VERB_CUBIC ? 6 : 0;
            for (int k = 0; k < coords; k += 2) {
                float x = mOps[i + k], y = mOps[i + k + 1];
                l = Math.min(l, x);
                t = Math.min(t, y);
                r = Math.max(r, x);
                b = Math.max(b, y);
            }
            i += coords;
        }
        bounds.set(l, t, r, b);
    }

    public void incReserve(int extraPtCount) {}
}
