package android.graphics;

/**
 * A stretchable bitmap described by an aapt "npTc" chunk (Res_png_9patch,
 * big-endian): stretchable x/y ranges and content padding. Drawing splits the
 * bitmap into fixed and stretched segments.
 */
public class NinePatch {
    private final Bitmap mBitmap;
    private final byte[] mChunk;
    private final int[] mXDivs;
    private final int[] mYDivs;
    private Paint mPaint;

    public NinePatch(Bitmap bitmap, byte[] chunk) {
        this(bitmap, chunk, null);
    }

    public NinePatch(Bitmap bitmap, byte[] chunk, String srcName) {
        mBitmap = bitmap;
        mChunk = chunk;
        mXDivs = divs(chunk, true);
        mYDivs = divs(chunk, false);
    }

    public static boolean isNinePatchChunk(byte[] chunk) {
        return chunk != null && chunk.length >= 32 && chunk.length >= 32 + 4 * ((chunk[1] & 0xff) + (chunk[2] & 0xff));
    }

    private static int be32(byte[] c, int off) {
        return ((c[off] & 0xff) << 24) | ((c[off + 1] & 0xff) << 16) | ((c[off + 2] & 0xff) << 8) | (c[off + 3] & 0xff);
    }

    private static int[] divs(byte[] chunk, boolean x) {
        if (!isNinePatchChunk(chunk)) return new int[0];
        int nx = chunk[1] & 0xff;
        int ny = chunk[2] & 0xff;
        int n = x ? nx : ny;
        int base = 32 + (x ? 0 : 4 * nx);
        int[] out = new int[n];
        for (int i = 0; i < n; i++) out[i] = be32(chunk, base + 4 * i);
        return out;
    }

    static void readPadding(byte[] chunk, Rect outPadding) {
        if (!isNinePatchChunk(chunk)) return;
        outPadding.set(be32(chunk, 12), be32(chunk, 20), be32(chunk, 16), be32(chunk, 24));
    }

    /** The npTc chunk of a PNG, or null. */
    static byte[] findChunk(byte[] data, int offset, int length) {
        if (length < 8 || (data[offset] & 0xff) != 0x89 || data[offset + 1] != 'P') return null;
        int pos = offset + 8;
        int end = offset + length;
        while (pos + 12 <= end) {
            int len = be32(data, pos);
            if (len < 0 || pos + 12L + len > end) return null;
            if (data[pos + 4] == 'n' && data[pos + 5] == 'p' && data[pos + 6] == 'T' && data[pos + 7] == 'c') {
                byte[] chunk = new byte[len];
                System.arraycopy(data, pos + 8, chunk, 0, len);
                return isNinePatchChunk(chunk) ? chunk : null;
            }
            if (data[pos + 4] == 'I' && data[pos + 5] == 'D' && data[pos + 6] == 'A' && data[pos + 7] == 'T') {
                return null;  // npTc precedes the image data
            }
            pos += 12 + len;
        }
        return null;
    }

    /** The chunk with divs and padding scaled (density scaling of the bitmap). */
    static byte[] scaleChunk(byte[] chunk, float sx, float sy) {
        if (!isNinePatchChunk(chunk)) return chunk;
        byte[] out = chunk.clone();
        int nx = chunk[1] & 0xff;
        int ny = chunk[2] & 0xff;
        for (int i = 0; i < nx + ny; i++) {
            float s = i < nx ? sx : sy;
            put32(out, 32 + 4 * i, (int) (be32(chunk, 32 + 4 * i) * s + 0.5f));
        }
        put32(out, 12, (int) (be32(chunk, 12) * sx + 0.5f));
        put32(out, 16, (int) (be32(chunk, 16) * sx + 0.5f));
        put32(out, 20, (int) (be32(chunk, 20) * sy + 0.5f));
        put32(out, 24, (int) (be32(chunk, 24) * sy + 0.5f));
        return out;
    }

    private static void put32(byte[] c, int off, int v) {
        c[off] = (byte) (v >>> 24);
        c[off + 1] = (byte) (v >>> 16);
        c[off + 2] = (byte) (v >>> 8);
        c[off + 3] = (byte) v;
    }

    public Bitmap getBitmap() {
        return mBitmap;
    }

    public int getWidth() {
        return mBitmap.getWidth();
    }

    public int getHeight() {
        return mBitmap.getHeight();
    }

    public void setPaint(Paint p) {
        mPaint = p;
    }

    public Paint getPaint() {
        return mPaint;
    }

    public final boolean hasAlpha() {
        return mBitmap.hasAlpha();
    }

    public void getPadding(Rect outPadding) {
        readPadding(mChunk, outPadding);
    }

    public void draw(Canvas canvas, RectF location) {
        draw(canvas, location, mPaint);
    }

    public void draw(Canvas canvas, Rect location) {
        draw(canvas, new RectF(location), mPaint);
    }

    public void draw(Canvas canvas, Rect location, Paint paint) {
        draw(canvas, new RectF(location), paint);
    }

    private void draw(Canvas canvas, RectF dst, Paint paint) {
        float[] xs = segments(mXDivs, mBitmap.getWidth(), dst.left, dst.right);
        float[] ys = segments(mYDivs, mBitmap.getHeight(), dst.top, dst.bottom);
        Rect src = new Rect();
        RectF to = new RectF();
        for (int yi = 0; yi + 3 < ys.length; yi += 4) {
            for (int xi = 0; xi + 3 < xs.length; xi += 4) {
                if (xs[xi + 1] <= xs[xi] || ys[yi + 1] <= ys[yi]) continue;
                if (xs[xi + 3] <= xs[xi + 2] || ys[yi + 3] <= ys[yi + 2]) continue;
                src.set((int) xs[xi], (int) ys[yi], (int) xs[xi + 1], (int) ys[yi + 1]);
                to.set(xs[xi + 2], ys[yi + 2], xs[xi + 3], ys[yi + 3]);
                canvas.drawBitmap(mBitmap, src, to, paint);
            }
        }
    }

    /**
     * Source and destination ranges along one axis: groups of
     * {srcStart, srcEnd, dstStart, dstEnd}.
     */
    private static float[] segments(int[] divs, int size, float dstStart, float dstEnd) {
        int[] bounds = new int[divs.length + 2];
        bounds[0] = 0;
        int n = 1;
        for (int d : divs) {
            if (d > bounds[n - 1] && d < size) bounds[n++] = d;
        }
        bounds[n++] = size;
        float fixed = 0, stretch = 0;
        for (int i = 0; i + 1 < n; i++) {
            float len = bounds[i + 1] - bounds[i];
            if (i % 2 == 1) stretch += len; else fixed += len;
        }
        float avail = dstEnd - dstStart;
        float fixedScale = fixed > avail && fixed > 0 ? avail / fixed : 1;
        float stretchSpace = Math.max(0, avail - fixed * fixedScale);
        float[] out = new float[(n - 1) * 4];
        float at = dstStart;
        for (int i = 0; i + 1 < n; i++) {
            float len = bounds[i + 1] - bounds[i];
            float dlen = i % 2 == 1 ? (stretch > 0 ? stretchSpace * len / stretch : 0) : len * fixedScale;
            out[i * 4] = bounds[i];
            out[i * 4 + 1] = bounds[i + 1];
            out[i * 4 + 2] = at;
            out[i * 4 + 3] = at + dlen;
            at += dlen;
        }
        return out;
    }
}
