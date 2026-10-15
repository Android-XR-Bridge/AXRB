package android.graphics;

import android.util.DisplayMetrics;
import java.util.ArrayList;

/**
 * Draws into a host pixel buffer: a window surface or a Bitmap. The matrix is
 * a full affine transform; clipping is to device-space rectangles (a clip in a
 * rotated space clips to its bounding box). A Canvas without a target
 * discards what is drawn, as a Canvas without a Bitmap does on Android.
 * Save layers are approximated by applying their alpha to what is drawn
 * inside them.
 */
public class Canvas {
    public static final int ALL_SAVE_FLAG = 0x1F;
    public static final int MATRIX_SAVE_FLAG = 0x01;
    public static final int CLIP_SAVE_FLAG = 0x02;

    public enum EdgeType {
        BW, AA
    }

    public enum VertexMode {
        TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN
    }

    private long mTarget;
    private Bitmap mBitmap;
    private int mWidth;
    private int mHeight;
    private int mDensity = Bitmap.DENSITY_NONE;

    private final Matrix mMatrix = new Matrix();
    private float mClipL, mClipT, mClipR, mClipB;
    private float mAlpha = 1f;
    private final ArrayList<float[]> mSaves = new ArrayList<float[]>();
    private final float[] mState = new float[11];

    public Canvas() {
        this(0L, 0, 0);
    }

    public Canvas(Bitmap bitmap) {
        if (!bitmap.isMutable()) throw new IllegalStateException("Immutable bitmap passed to Canvas constructor");
        mBitmap = bitmap;
        mTarget = bitmap.mNativePtr;
        mWidth = bitmap.getWidth();
        mHeight = bitmap.getHeight();
        mDensity = bitmap.getDensity();
        resetClip();
    }

    /** Runtime-internal: a canvas over host surface `target` (see ViewRootImpl). */
    public Canvas(long target, int width, int height) {
        mTarget = target;
        mWidth = width;
        mHeight = height;
        mDensity = DisplayMetrics.DENSITY_DEVICE_STABLE;
        resetClip();
    }

    private void resetClip() {
        mClipL = 0;
        mClipT = 0;
        mClipR = mWidth;
        mClipB = mHeight;
    }

    public void setBitmap(Bitmap bitmap) {
        if (bitmap != null && !bitmap.isMutable()) throw new IllegalStateException();
        mBitmap = bitmap;
        mTarget = bitmap != null ? bitmap.mNativePtr : 0;
        mWidth = bitmap != null ? bitmap.getWidth() : 0;
        mHeight = bitmap != null ? bitmap.getHeight() : 0;
        mDensity = bitmap != null ? bitmap.getDensity() : Bitmap.DENSITY_NONE;
        mMatrix.reset();
        mSaves.clear();
        mAlpha = 1f;
        resetClip();
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public int getDensity() {
        return mDensity;
    }

    public void setDensity(int density) {
        mDensity = density;
    }

    public boolean isHardwareAccelerated() {
        return false;
    }

    public boolean isOpaque() {
        return mBitmap == null;
    }

    public int getMaximumBitmapWidth() {
        return 16384;
    }

    public int getMaximumBitmapHeight() {
        return 16384;
    }

    // --- save / restore ---------------------------------------------------------

    public int save() {
        float[] s = new float[14];
        mMatrix.getValues(s);
        s[9] = mClipL;
        s[10] = mClipT;
        s[11] = mClipR;
        s[12] = mClipB;
        s[13] = mAlpha;
        mSaves.add(s);
        return mSaves.size();
    }

    public int save(int saveFlags) {
        return save();
    }

    public int saveLayer(RectF bounds, Paint paint) {
        int count = save();
        if (paint != null) mAlpha *= paint.getAlpha() / 255f;
        return count;
    }

    public int saveLayer(RectF bounds, Paint paint, int saveFlags) {
        return saveLayer(bounds, paint);
    }

    public int saveLayer(float left, float top, float right, float bottom, Paint paint) {
        return saveLayer(null, paint);
    }

    public int saveLayer(float left, float top, float right, float bottom, Paint paint, int saveFlags) {
        return saveLayer(null, paint);
    }

    public int saveLayerAlpha(RectF bounds, int alpha) {
        int count = save();
        mAlpha *= Math.max(0, Math.min(255, alpha)) / 255f;
        return count;
    }

    public int saveLayerAlpha(RectF bounds, int alpha, int saveFlags) {
        return saveLayerAlpha(bounds, alpha);
    }

    public int saveLayerAlpha(float left, float top, float right, float bottom, int alpha) {
        return saveLayerAlpha(null, alpha);
    }

    public int saveLayerAlpha(float left, float top, float right, float bottom, int alpha, int saveFlags) {
        return saveLayerAlpha(null, alpha);
    }

    public int getSaveCount() {
        return mSaves.size() + 1;
    }

    public void restore() {
        if (mSaves.isEmpty()) throw new IllegalStateException("Underflow in restore - more restores than saves");
        float[] s = mSaves.remove(mSaves.size() - 1);
        float[] m = new float[9];
        System.arraycopy(s, 0, m, 0, 9);
        mMatrix.setValues(m);
        mClipL = s[9];
        mClipT = s[10];
        mClipR = s[11];
        mClipB = s[12];
        mAlpha = s[13];
    }

    public void restoreToCount(int saveCount) {
        if (saveCount < 1) throw new IllegalArgumentException("Underflow in restoreToCount - more restores than saves");
        while (getSaveCount() > saveCount) restore();
    }

    // --- matrix -------------------------------------------------------------------

    public void translate(float dx, float dy) {
        if (dx != 0 || dy != 0) mMatrix.preTranslate(dx, dy);
    }

    public void scale(float sx, float sy) {
        if (sx != 1 || sy != 1) mMatrix.preScale(sx, sy);
    }

    public final void scale(float sx, float sy, float px, float py) {
        if (sx == 1 && sy == 1) return;
        translate(px, py);
        scale(sx, sy);
        translate(-px, -py);
    }

    public void rotate(float degrees) {
        if (degrees != 0) mMatrix.preRotate(degrees);
    }

    public final void rotate(float degrees, float px, float py) {
        if (degrees == 0) return;
        translate(px, py);
        rotate(degrees);
        translate(-px, -py);
    }

    public void skew(float sx, float sy) {
        if (sx != 0 || sy != 0) mMatrix.preSkew(sx, sy);
    }

    public void concat(Matrix matrix) {
        if (matrix != null) mMatrix.preConcat(matrix);
    }

    public void setMatrix(Matrix matrix) {
        if (matrix == null) mMatrix.reset(); else mMatrix.set(matrix);
    }

    @Deprecated
    public void getMatrix(Matrix ctm) {
        ctm.set(mMatrix);
    }

    public final Matrix getMatrix() {
        return new Matrix(mMatrix);
    }

    // --- clip -----------------------------------------------------------------------

    public boolean clipRect(float left, float top, float right, float bottom) {
        RectF r = new RectF(left, top, right, bottom);
        mMatrix.mapRect(r);
        mClipL = Math.max(mClipL, (float) Math.floor(r.left + 0.001f));
        mClipT = Math.max(mClipT, (float) Math.floor(r.top + 0.001f));
        mClipR = Math.min(mClipR, (float) Math.ceil(r.right - 0.001f));
        mClipB = Math.min(mClipB, (float) Math.ceil(r.bottom - 0.001f));
        return mClipL < mClipR && mClipT < mClipB;
    }

    public boolean clipRect(int left, int top, int right, int bottom) {
        return clipRect((float) left, (float) top, (float) right, (float) bottom);
    }

    public boolean clipRect(Rect rect) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom);
    }

    public boolean clipRect(RectF rect) {
        return clipRect(rect.left, rect.top, rect.right, rect.bottom);
    }

    public boolean clipRect(RectF rect, Region.Op op) {
        if (op == Region.Op.INTERSECT) return clipRect(rect);
        return !isClipEmpty();
    }

    public boolean clipRect(Rect rect, Region.Op op) {
        if (op == Region.Op.INTERSECT) return clipRect(rect);
        return !isClipEmpty();
    }

    public boolean clipRect(float left, float top, float right, float bottom, Region.Op op) {
        if (op == Region.Op.INTERSECT) return clipRect(left, top, right, bottom);
        return !isClipEmpty();
    }

    public boolean clipOutRect(RectF rect) {
        return !isClipEmpty();
    }

    public boolean clipOutRect(Rect rect) {
        return !isClipEmpty();
    }

    /** Clips to the path's bounds (no non-rectangular clipping yet). */
    public boolean clipPath(Path path) {
        RectF b = new RectF();
        path.computeBounds(b, true);
        return clipRect(b);
    }

    public boolean clipPath(Path path, Region.Op op) {
        if (op == Region.Op.INTERSECT) return clipPath(path);
        return !isClipEmpty();
    }

    public boolean clipOutPath(Path path) {
        return !isClipEmpty();
    }

    public boolean isClipEmpty() {
        return mClipL >= mClipR || mClipT >= mClipB;
    }

    public boolean getClipBounds(Rect bounds) {
        Matrix inverse = new Matrix();
        RectF r = new RectF(mClipL, mClipT, mClipR, mClipB);
        if (mMatrix.invert(inverse)) inverse.mapRect(r);
        bounds.set((int) Math.floor(r.left), (int) Math.floor(r.top), (int) Math.ceil(r.right),
                (int) Math.ceil(r.bottom));
        return !isClipEmpty();
    }

    public final Rect getClipBounds() {
        Rect r = new Rect();
        getClipBounds(r);
        return r;
    }

    public boolean quickReject(float left, float top, float right, float bottom, EdgeType type) {
        if (isClipEmpty()) return true;
        RectF r = new RectF(left, top, right, bottom);
        mMatrix.mapRect(r);
        return r.right <= mClipL || r.left >= mClipR || r.bottom <= mClipT || r.top >= mClipB;
    }

    public boolean quickReject(float left, float top, float right, float bottom) {
        return quickReject(left, top, right, bottom, EdgeType.AA);
    }

    public boolean quickReject(RectF rect, EdgeType type) {
        return quickReject(rect.left, rect.top, rect.right, rect.bottom, type);
    }

    public boolean quickReject(Path path, EdgeType type) {
        RectF b = new RectF();
        path.computeBounds(b, false);
        return quickReject(b, type);
    }

    // --- state to host ----------------------------------------------------------------

    private boolean skip() {
        return mTarget == 0 || isClipEmpty() || mAlpha <= 0;
    }

    private float[] state() {
        float[] m = mMatrix.m;
        mState[0] = m[Matrix.MSCALE_X];
        mState[1] = m[Matrix.MSKEW_Y];
        mState[2] = m[Matrix.MSKEW_X];
        mState[3] = m[Matrix.MSCALE_Y];
        mState[4] = m[Matrix.MTRANS_X];
        mState[5] = m[Matrix.MTRANS_Y];
        mState[6] = mClipL;
        mState[7] = mClipT;
        mState[8] = mClipR;
        mState[9] = mClipB;
        mState[10] = mAlpha;
        return mState;
    }

    // --- drawing --------------------------------------------------------------------------

    public void drawColor(int color) {
        if (skip()) return;
        nativeClear(mTarget, state(), color, false);
    }

    public void drawColor(int color, PorterDuff.Mode mode) {
        if (skip()) return;
        if (mode == PorterDuff.Mode.CLEAR) {
            nativeClear(mTarget, state(), 0, true);
        } else if (mode == PorterDuff.Mode.SRC) {
            nativeClear(mTarget, state(), color, true);
        } else {
            nativeClear(mTarget, state(), color, false);
        }
    }

    public void drawRGB(int r, int g, int b) {
        drawColor(Color.rgb(r, g, b));
    }

    public void drawARGB(int a, int r, int g, int b) {
        drawColor(Color.argb(a, r, g, b));
    }

    public void drawPaint(Paint paint) {
        if (skip()) return;
        if (paint.getShader() == null) {
            nativeClear(mTarget, state(), paint.hostColor(), false);
            return;
        }
        Rect b = getClipBounds();
        drawRect(b.left, b.top, b.right, b.bottom, paint);
    }

    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
        drawRoundRect(left, top, right, bottom, 0, 0, paint);
    }

    public void drawRect(Rect r, Paint paint) {
        drawRect(r.left, r.top, r.right, r.bottom, paint);
    }

    public void drawRect(RectF r, Paint paint) {
        drawRect(r.left, r.top, r.right, r.bottom, paint);
    }

    public void drawRoundRect(RectF rect, float rx, float ry, Paint paint) {
        drawRoundRect(rect.left, rect.top, rect.right, rect.bottom, rx, ry, paint);
    }

    public void drawRoundRect(float left, float top, float right, float bottom, float rx, float ry, Paint paint) {
        if (skip() || !(left < right) || !(top < bottom)) return;
        nativeDrawRect(mTarget, state(), left, top, right, bottom, Math.max(0, rx), Math.max(0, ry),
                paint.hostColor(), paint.hostFlags(), paint.getStrokeWidth(), paint.getStrokeMiter(),
                paint.hostShader());
    }

    public void drawOval(RectF oval, Paint paint) {
        drawOval(oval.left, oval.top, oval.right, oval.bottom, paint);
    }

    public void drawOval(float left, float top, float right, float bottom, Paint paint) {
        if (skip() || !(left < right) || !(top < bottom)) return;
        nativeDrawOval(mTarget, state(), left, top, right, bottom, paint.hostColor(), paint.hostFlags(),
                paint.getStrokeWidth(), paint.getStrokeMiter(), paint.hostShader());
    }

    public void drawCircle(float cx, float cy, float radius, Paint paint) {
        if (radius <= 0) return;
        drawOval(cx - radius, cy - radius, cx + radius, cy + radius, paint);
    }

    public void drawArc(RectF oval, float startAngle, float sweepAngle, boolean useCenter, Paint paint) {
        drawArc(oval.left, oval.top, oval.right, oval.bottom, startAngle, sweepAngle, useCenter, paint);
    }

    public void drawArc(float left, float top, float right, float bottom, float startAngle, float sweepAngle,
            boolean useCenter, Paint paint) {
        if (skip() || !(left < right) || !(top < bottom) || sweepAngle == 0) return;
        nativeDrawArc(mTarget, state(), left, top, right, bottom, startAngle, sweepAngle, useCenter,
                paint.hostColor(), paint.hostFlags(), paint.getStrokeWidth(), paint.getStrokeMiter(),
                paint.hostShader());
    }

    public void drawLine(float startX, float startY, float stopX, float stopY, Paint paint) {
        if (skip()) return;
        // Lines are always stroked, whatever the paint style.
        int flags = (paint.hostFlags() & ~(3 << 8)) | (1 << 8);
        nativeDrawLine(mTarget, state(), startX, startY, stopX, stopY, paint.hostColor(), flags,
                paint.getStrokeWidth(), paint.getStrokeMiter(), paint.hostShader());
    }

    public void drawLines(float[] pts, int offset, int count, Paint paint) {
        for (int i = offset; i + 3 < offset + count; i += 4) drawLine(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], paint);
    }

    public void drawLines(float[] pts, Paint paint) {
        drawLines(pts, 0, pts.length, paint);
    }

    public void drawPoint(float x, float y, Paint paint) {
        float r = Math.max(1f, paint.getStrokeWidth()) / 2;
        if (paint.getStrokeCap() == Paint.Cap.ROUND) {
            Paint p = new Paint(paint);
            p.setStyle(Paint.Style.FILL);
            drawCircle(x, y, r, p);
        } else {
            Paint p = new Paint(paint);
            p.setStyle(Paint.Style.FILL);
            drawRect(x - r, y - r, x + r, y + r, p);
        }
    }

    public void drawPoints(float[] pts, int offset, int count, Paint paint) {
        for (int i = offset; i + 1 < offset + count; i += 2) drawPoint(pts[i], pts[i + 1], paint);
    }

    public void drawPoints(float[] pts, Paint paint) {
        drawPoints(pts, 0, pts.length, paint);
    }

    public void drawPath(Path path, Paint paint) {
        if (skip() || path.isEmpty()) return;
        Path.FillType ft = path.getFillType();
        boolean evenOdd = ft == Path.FillType.EVEN_ODD || ft == Path.FillType.INVERSE_EVEN_ODD;
        nativeDrawPath(mTarget, state(), path.mOps, path.mCount, evenOdd, paint.hostColor(), paint.hostFlags(),
                paint.getStrokeWidth(), paint.getStrokeMiter(), paint.hostShader());
    }

    // --- bitmaps ------------------------------------------------------------------------------

    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
        if (skip()) return;
        int w = bitmap.getWidth(), h = bitmap.getHeight();
        float dw = w, dh = h;
        if (mDensity != Bitmap.DENSITY_NONE && bitmap.getDensity() != Bitmap.DENSITY_NONE
                && bitmap.getDensity() != mDensity) {
            dw = Bitmap.scaleFromDensity(w, bitmap.getDensity(), mDensity);
            dh = Bitmap.scaleFromDensity(h, bitmap.getDensity(), mDensity);
        }
        bitmap(bitmap, 0, 0, w, h, left, top, left + dw, top + dh, paint);
    }

    public void drawBitmap(Bitmap bitmap, Rect src, RectF dst, Paint paint) {
        if (skip()) return;
        if (src == null) {
            bitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), dst.left, dst.top, dst.right, dst.bottom, paint);
        } else {
            bitmap(bitmap, src.left, src.top, src.right, src.bottom, dst.left, dst.top, dst.right, dst.bottom, paint);
        }
    }

    public void drawBitmap(Bitmap bitmap, Rect src, Rect dst, Paint paint) {
        if (skip()) return;
        if (src == null) {
            bitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), dst.left, dst.top, dst.right, dst.bottom, paint);
        } else {
            bitmap(bitmap, src.left, src.top, src.right, src.bottom, dst.left, dst.top, dst.right, dst.bottom, paint);
        }
    }

    public void drawBitmap(Bitmap bitmap, Matrix matrix, Paint paint) {
        if (skip()) return;
        save();
        concat(matrix);
        bitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), 0, 0, bitmap.getWidth(), bitmap.getHeight(), paint);
        restore();
    }

    public void drawBitmap(int[] colors, int offset, int stride, float x, float y, int width, int height,
            boolean hasAlpha, Paint paint) {
        if (width <= 0 || height <= 0) return;
        Bitmap b = Bitmap.createBitmap(colors, offset, stride, width, height, Bitmap.Config.ARGB_8888);
        drawBitmap(b, x, y, paint);
    }

    private void bitmap(Bitmap bitmap, float sl, float st, float sr, float sb, float dl, float dt, float dr, float db,
            Paint paint) {
        if (bitmap.isRecycled()) throw new RuntimeException("Canvas: trying to use a recycled bitmap " + bitmap);
        if (!(dl < dr) || !(dt < db)) return;
        int alpha = paint != null ? paint.getAlpha() : 255;
        boolean filter = paint == null || paint.isFilterBitmap() || !mMatrix.isIdentity() || (sr - sl) != (dr - dl)
                || (sb - st) != (db - dt);
        nativeDrawBitmap(mTarget, state(), bitmap.mNativePtr, sl, st, sr, sb, dl, dt, dr, db, alpha, filter);
    }

    // --- text ------------------------------------------------------------------------------------

    public void drawText(String text, float x, float y, Paint paint) {
        drawTextRun(text, x, y, paint);
    }

    public void drawText(String text, int start, int end, float x, float y, Paint paint) {
        if ((start | end | (end - start) | (text.length() - end)) < 0) throw new IndexOutOfBoundsException();
        drawTextRun(text.substring(start, end), x, y, paint);
    }

    public void drawText(CharSequence text, int start, int end, float x, float y, Paint paint) {
        if ((start | end | (end - start) | (text.length() - end)) < 0) throw new IndexOutOfBoundsException();
        drawTextRun(text.subSequence(start, end).toString(), x, y, paint);
    }

    public void drawText(char[] text, int index, int count, float x, float y, Paint paint) {
        if ((index | count) < 0 || index + count > text.length) throw new IndexOutOfBoundsException();
        drawTextRun(new String(text, index, count), x, y, paint);
    }

    public void drawTextRun(CharSequence text, int start, int end, int contextStart, int contextEnd, float x, float y,
            boolean isRtl, Paint paint) {
        drawText(text, start, end, x, y, paint);
    }

    public void drawTextOnPath(String text, Path path, float hOffset, float vOffset, Paint paint) {
        RectF b = new RectF();
        path.computeBounds(b, false);
        drawText(text, b.left + hOffset, b.top + vOffset, paint);
    }

    private void drawTextRun(String s, float x, float y, Paint paint) {
        if (skip() || s.length() == 0) return;
        Paint.Align align = paint.getTextAlign();
        if (align != Paint.Align.LEFT) {
            float w = paint.measureText(s);
            x -= align == Paint.Align.CENTER ? w / 2 : w;
        }
        if (paint.getTextScaleX() != 1f) {
            save();
            scale(paint.getTextScaleX(), 1, x, y);
            nativeDrawText(mTarget, state(), s, x, y, paint.getTextSize(), paint.hostWeight(), paint.hostFamily(),
                    paint.hostItalic(), paint.hostColor(), paint.hostFlags(), paint.hostShader());
            restore();
            return;
        }
        nativeDrawText(mTarget, state(), s, x, y, paint.getTextSize(), paint.hostWeight(), paint.hostFamily(),
                paint.hostItalic(), paint.hostColor(), paint.hostFlags(), paint.hostShader());
    }

    // --- natives ---------------------------------------------------------------------------------
    // state = {m11 m12 m21 m22 dx dy, clipL clipT clipR clipB, alpha}

    private static native void nativeClear(long target, float[] state, int color, boolean srcMode);

    private static native void nativeDrawRect(long target, float[] state, float l, float t, float r, float b,
            float rx, float ry, int color, int flags, float strokeWidth, float miter, float[] shader);

    private static native void nativeDrawOval(long target, float[] state, float l, float t, float r, float b,
            int color, int flags, float strokeWidth, float miter, float[] shader);

    private static native void nativeDrawArc(long target, float[] state, float l, float t, float r, float b,
            float start, float sweep, boolean useCenter, int color, int flags, float strokeWidth, float miter,
            float[] shader);

    private static native void nativeDrawLine(long target, float[] state, float x0, float y0, float x1, float y1,
            int color, int flags, float strokeWidth, float miter, float[] shader);

    private static native void nativeDrawPath(long target, float[] state, float[] ops, int count, boolean evenOdd,
            int color, int flags, float strokeWidth, float miter, float[] shader);

    private static native void nativeDrawBitmap(long target, float[] state, long bitmap, float sl, float st, float sr,
            float sb, float dl, float dt, float dr, float db, int alpha, boolean filter);

    private static native void nativeDrawText(long target, float[] state, String text, float x, float y, float size,
            int weight, int family, boolean italic, int color, int flags, float[] shader);
}
