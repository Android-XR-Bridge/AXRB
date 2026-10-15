package android.graphics;

import java.util.Arrays;

/**
 * A 3x3 transformation matrix. Perspective values are stored and composed,
 * but the renderer draws affine transforms only (the perspective row is
 * ignored when drawing).
 */
public class Matrix {
    public static final int MSCALE_X = 0;
    public static final int MSKEW_X = 1;
    public static final int MTRANS_X = 2;
    public static final int MSKEW_Y = 3;
    public static final int MSCALE_Y = 4;
    public static final int MTRANS_Y = 5;
    public static final int MPERSP_0 = 6;
    public static final int MPERSP_1 = 7;
    public static final int MPERSP_2 = 8;

    public static final Matrix IDENTITY_MATRIX = new Matrix() {
        @Override
        void checkMutable() {
            throw new IllegalStateException("Matrix can not be modified");
        }
    };

    public enum ScaleToFit {
        FILL, START, CENTER, END
    }

    final float[] m = new float[] {1, 0, 0, 0, 1, 0, 0, 0, 1};

    public Matrix() {}

    public Matrix(Matrix src) {
        if (src != null) System.arraycopy(src.m, 0, m, 0, 9);
    }

    void checkMutable() {}

    public boolean isIdentity() {
        return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0 && m[6] == 0 && m[7] == 0
                && m[8] == 1;
    }

    public boolean isAffine() {
        return m[6] == 0 && m[7] == 0 && m[8] == 1;
    }

    public boolean rectStaysRect() {
        return (m[1] == 0 && m[3] == 0) || (m[0] == 0 && m[4] == 0);
    }

    public void set(Matrix src) {
        checkMutable();
        if (src == null) reset(); else System.arraycopy(src.m, 0, m, 0, 9);
    }

    public void reset() {
        checkMutable();
        setIdentity(m);
    }

    private static void setIdentity(float[] a) {
        a[0] = 1; a[1] = 0; a[2] = 0;
        a[3] = 0; a[4] = 1; a[5] = 0;
        a[6] = 0; a[7] = 0; a[8] = 1;
    }

    public void setTranslate(float dx, float dy) {
        checkMutable();
        setIdentity(m);
        m[2] = dx;
        m[5] = dy;
    }

    public void setScale(float sx, float sy, float px, float py) {
        checkMutable();
        setIdentity(m);
        m[0] = sx;
        m[4] = sy;
        m[2] = px - sx * px;
        m[5] = py - sy * py;
    }

    public void setScale(float sx, float sy) {
        checkMutable();
        setIdentity(m);
        m[0] = sx;
        m[4] = sy;
    }

    public void setRotate(float degrees, float px, float py) {
        double rad = Math.toRadians(degrees);
        setSinCos((float) Math.sin(rad), (float) Math.cos(rad), px, py);
    }

    public void setRotate(float degrees) {
        setRotate(degrees, 0, 0);
    }

    public void setSinCos(float sinValue, float cosValue, float px, float py) {
        checkMutable();
        setIdentity(m);
        m[0] = cosValue;
        m[1] = -sinValue;
        m[3] = sinValue;
        m[4] = cosValue;
        m[2] = px - cosValue * px + sinValue * py;
        m[5] = py - sinValue * px - cosValue * py;
    }

    public void setSinCos(float sinValue, float cosValue) {
        setSinCos(sinValue, cosValue, 0, 0);
    }

    public void setSkew(float kx, float ky, float px, float py) {
        checkMutable();
        setIdentity(m);
        m[1] = kx;
        m[3] = ky;
        m[2] = -kx * py;
        m[5] = -ky * px;
    }

    public void setSkew(float kx, float ky) {
        setSkew(kx, ky, 0, 0);
    }

    public boolean setConcat(Matrix a, Matrix b) {
        checkMutable();
        float[] r = new float[9];
        multiply(a.m, b.m, r);
        System.arraycopy(r, 0, m, 0, 9);
        return true;
    }

    static void multiply(float[] a, float[] b, float[] out) {
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                out[row * 3 + col] = a[row * 3] * b[col] + a[row * 3 + 1] * b[3 + col] + a[row * 3 + 2] * b[6 + col];
            }
        }
    }

    private void pre(Matrix other) {
        float[] r = new float[9];
        multiply(m, other.m, r);
        System.arraycopy(r, 0, m, 0, 9);
    }

    private void post(Matrix other) {
        float[] r = new float[9];
        multiply(other.m, m, r);
        System.arraycopy(r, 0, m, 0, 9);
    }

    public boolean preTranslate(float dx, float dy) {
        checkMutable();
        Matrix t = new Matrix();
        t.setTranslate(dx, dy);
        pre(t);
        return true;
    }

    public boolean preScale(float sx, float sy, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setScale(sx, sy, px, py);
        pre(t);
        return true;
    }

    public boolean preScale(float sx, float sy) {
        checkMutable();
        Matrix t = new Matrix();
        t.setScale(sx, sy);
        pre(t);
        return true;
    }

    public boolean preRotate(float degrees, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setRotate(degrees, px, py);
        pre(t);
        return true;
    }

    public boolean preRotate(float degrees) {
        return preRotate(degrees, 0, 0);
    }

    public boolean preSkew(float kx, float ky, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setSkew(kx, ky, px, py);
        pre(t);
        return true;
    }

    public boolean preSkew(float kx, float ky) {
        return preSkew(kx, ky, 0, 0);
    }

    public boolean preConcat(Matrix other) {
        checkMutable();
        pre(other);
        return true;
    }

    public boolean postTranslate(float dx, float dy) {
        checkMutable();
        Matrix t = new Matrix();
        t.setTranslate(dx, dy);
        post(t);
        return true;
    }

    public boolean postScale(float sx, float sy, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setScale(sx, sy, px, py);
        post(t);
        return true;
    }

    public boolean postScale(float sx, float sy) {
        checkMutable();
        Matrix t = new Matrix();
        t.setScale(sx, sy);
        post(t);
        return true;
    }

    public boolean postRotate(float degrees, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setRotate(degrees, px, py);
        post(t);
        return true;
    }

    public boolean postRotate(float degrees) {
        return postRotate(degrees, 0, 0);
    }

    public boolean postSkew(float kx, float ky, float px, float py) {
        checkMutable();
        Matrix t = new Matrix();
        t.setSkew(kx, ky, px, py);
        post(t);
        return true;
    }

    public boolean postSkew(float kx, float ky) {
        return postSkew(kx, ky, 0, 0);
    }

    public boolean postConcat(Matrix other) {
        checkMutable();
        post(other);
        return true;
    }

    public boolean setRectToRect(RectF src, RectF dst, ScaleToFit stf) {
        checkMutable();
        if (dst == null || src == null) throw new NullPointerException();
        if (src.isEmpty()) {
            reset();
            return false;
        }
        if (dst.isEmpty()) {
            Arrays.fill(m, 0, 8, 0);
            m[8] = 1;
            return true;
        }
        float sx = dst.width() / src.width();
        float sy = dst.height() / src.height();
        boolean xLarger = false;
        if (stf != ScaleToFit.FILL) {
            if (sx > sy) {
                xLarger = true;
                sx = sy;
            } else {
                sy = sx;
            }
        }
        float tx = dst.left - src.left * sx;
        float ty = dst.top - src.top * sy;
        if (stf == ScaleToFit.CENTER || stf == ScaleToFit.END) {
            float diff;
            if (xLarger) {
                diff = dst.width() - src.width() * sy;
            } else {
                diff = dst.height() - src.height() * sy;
            }
            if (stf == ScaleToFit.CENTER) diff = diff / 2;
            if (xLarger) tx += diff; else ty += diff;
        }
        setIdentity(m);
        m[0] = sx;
        m[4] = sy;
        m[2] = tx;
        m[5] = ty;
        return true;
    }

    public boolean setPolyToPoly(float[] src, int srcIndex, float[] dst, int dstIndex, int pointCount) {
        checkMutable();
        if (pointCount < 0 || pointCount > 4) throw new IllegalArgumentException();
        reset();
        if (pointCount == 0) return true;
        if (pointCount == 1) {
            setTranslate(dst[dstIndex] - src[srcIndex], dst[dstIndex + 1] - src[srcIndex + 1]);
            return true;
        }
        // Affine fit through the first three points (or two plus a perpendicular).
        float sx0 = src[srcIndex], sy0 = src[srcIndex + 1], sx1 = src[srcIndex + 2], sy1 = src[srcIndex + 3];
        float dx0 = dst[dstIndex], dy0 = dst[dstIndex + 1], dx1 = dst[dstIndex + 2], dy1 = dst[dstIndex + 3];
        float sx2, sy2, dx2, dy2;
        if (pointCount >= 3) {
            sx2 = src[srcIndex + 4];
            sy2 = src[srcIndex + 5];
            dx2 = dst[dstIndex + 4];
            dy2 = dst[dstIndex + 5];
        } else {
            sx2 = sx0 - (sy1 - sy0);
            sy2 = sy0 + (sx1 - sx0);
            dx2 = dx0 - (dy1 - dy0);
            dy2 = dy0 + (dx1 - dx0);
        }
        Matrix s = new Matrix();
        s.m[0] = sx1 - sx0; s.m[1] = sx2 - sx0; s.m[2] = sx0;
        s.m[3] = sy1 - sy0; s.m[4] = sy2 - sy0; s.m[5] = sy0;
        Matrix d = new Matrix();
        d.m[0] = dx1 - dx0; d.m[1] = dx2 - dx0; d.m[2] = dx0;
        d.m[3] = dy1 - dy0; d.m[4] = dy2 - dy0; d.m[5] = dy0;
        Matrix inv = new Matrix();
        if (!s.invert(inv)) return false;
        setConcat(d, inv);
        return true;
    }

    public boolean invert(Matrix inverse) {
        float[] a = m;
        float det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6])
                + a[2] * (a[3] * a[7] - a[4] * a[6]);
        if (det == 0 || Float.isNaN(det)) return false;
        float inv = 1 / det;
        float[] r = new float[9];
        r[0] = (a[4] * a[8] - a[5] * a[7]) * inv;
        r[1] = (a[2] * a[7] - a[1] * a[8]) * inv;
        r[2] = (a[1] * a[5] - a[2] * a[4]) * inv;
        r[3] = (a[5] * a[6] - a[3] * a[8]) * inv;
        r[4] = (a[0] * a[8] - a[2] * a[6]) * inv;
        r[5] = (a[2] * a[3] - a[0] * a[5]) * inv;
        r[6] = (a[3] * a[7] - a[4] * a[6]) * inv;
        r[7] = (a[1] * a[6] - a[0] * a[7]) * inv;
        r[8] = (a[0] * a[4] - a[1] * a[3]) * inv;
        if (inverse != null) {
            inverse.checkMutable();
            System.arraycopy(r, 0, inverse.m, 0, 9);
        }
        return true;
    }

    public void mapPoints(float[] dst, int dstIndex, float[] src, int srcIndex, int pointCount) {
        for (int i = 0; i < pointCount; i++) {
            float x = src[srcIndex + i * 2];
            float y = src[srcIndex + i * 2 + 1];
            float w = m[6] * x + m[7] * y + m[8];
            if (w == 0) w = 1;
            dst[dstIndex + i * 2] = (m[0] * x + m[1] * y + m[2]) / w;
            dst[dstIndex + i * 2 + 1] = (m[3] * x + m[4] * y + m[5]) / w;
        }
    }

    public void mapPoints(float[] dst, float[] src) {
        mapPoints(dst, 0, src, 0, dst.length >> 1);
    }

    public void mapPoints(float[] pts) {
        mapPoints(pts, 0, pts, 0, pts.length >> 1);
    }

    public void mapVectors(float[] dst, int dstIndex, float[] src, int srcIndex, int vectorCount) {
        for (int i = 0; i < vectorCount; i++) {
            float x = src[srcIndex + i * 2];
            float y = src[srcIndex + i * 2 + 1];
            dst[dstIndex + i * 2] = m[0] * x + m[1] * y;
            dst[dstIndex + i * 2 + 1] = m[3] * x + m[4] * y;
        }
    }

    public void mapVectors(float[] vecs) {
        mapVectors(vecs, 0, vecs, 0, vecs.length >> 1);
    }

    public boolean mapRect(RectF dst, RectF src) {
        float[] pts = {src.left, src.top, src.right, src.top, src.right, src.bottom, src.left, src.bottom};
        mapPoints(pts);
        float l = Math.min(Math.min(pts[0], pts[2]), Math.min(pts[4], pts[6]));
        float r = Math.max(Math.max(pts[0], pts[2]), Math.max(pts[4], pts[6]));
        float t = Math.min(Math.min(pts[1], pts[3]), Math.min(pts[5], pts[7]));
        float b = Math.max(Math.max(pts[1], pts[3]), Math.max(pts[5], pts[7]));
        dst.set(l, t, r, b);
        return rectStaysRect();
    }

    public boolean mapRect(RectF rect) {
        return mapRect(rect, rect);
    }

    public float mapRadius(float radius) {
        float[] v = {radius, 0, 0, radius};
        mapVectors(v);
        float d0 = (float) Math.hypot(v[0], v[1]);
        float d1 = (float) Math.hypot(v[2], v[3]);
        return (float) Math.sqrt(d0 * d1);
    }

    public void getValues(float[] values) {
        if (values.length < 9) throw new ArrayIndexOutOfBoundsException();
        System.arraycopy(m, 0, values, 0, 9);
    }

    public void setValues(float[] values) {
        checkMutable();
        if (values.length < 9) throw new ArrayIndexOutOfBoundsException();
        System.arraycopy(values, 0, m, 0, 9);
    }

    @Override
    public boolean equals(Object obj) {
        return obj instanceof Matrix && Arrays.equals(m, ((Matrix) obj).m);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(m);
    }

    public String toShortString() {
        return "[" + m[0] + ", " + m[1] + ", " + m[2] + "][" + m[3] + ", " + m[4] + ", " + m[5] + "][" + m[6] + ", "
                + m[7] + ", " + m[8] + "]";
    }

    @Override
    public String toString() {
        return "Matrix{" + toShortString() + "}";
    }
}
