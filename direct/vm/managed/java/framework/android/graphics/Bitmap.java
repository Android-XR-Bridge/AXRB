package android.graphics;

import android.util.DisplayMetrics;
import java.io.IOException;
import java.io.OutputStream;

/**
 * A pixel buffer held by the host (32-bit premultiplied; every Config is
 * stored as ARGB_8888). The collector frees the pixels once the Bitmap is
 * unreachable; recycle() only marks it unusable.
 */
public final class Bitmap {
    public static final int DENSITY_NONE = 0;

    public enum Config {
        ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE
    }

    public enum CompressFormat {
        JPEG, PNG, WEBP, WEBP_LOSSY, WEBP_LOSSLESS
    }

    final long mNativePtr;
    private final int mWidth;
    private final int mHeight;
    private final Config mConfig;
    private boolean mMutable;
    private boolean mRecycled;
    private boolean mPremultiplied = true;
    int mDensity = sDefaultDensity();
    byte[] mNinePatchChunk;

    private static int sDefaultDensity() {
        return DisplayMetrics.DENSITY_DEVICE_STABLE;
    }

    Bitmap(long nativePtr, int width, int height, Config config, boolean mutable) {
        if (nativePtr == 0) throw new RuntimeException("internal error: native bitmap is 0");
        mNativePtr = nativePtr;
        mWidth = width;
        mHeight = height;
        mConfig = config != null ? config : Config.ARGB_8888;
        mMutable = mutable;
        nativeRegisterCleanup(this, nativePtr, (long) width * height * 4);
    }

    // --- creation ----------------------------------------------------------------

    public static Bitmap createBitmap(int width, int height, Config config) {
        return createBitmap(width, height, config, true);
    }

    public static Bitmap createBitmap(int width, int height, Config config, boolean hasAlpha) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("width and height must be > 0");
        if (config == Config.HARDWARE) throw new IllegalArgumentException("can't create mutable bitmap with Config.HARDWARE");
        boolean alpha = hasAlpha && config != Config.RGB_565;
        Bitmap b = new Bitmap(nativeCreate(width, height, alpha), width, height, config, true);
        if (alpha) b.eraseColor(0);
        return b;
    }

    public static Bitmap createBitmap(DisplayMetrics display, int width, int height, Config config) {
        Bitmap b = createBitmap(width, height, config);
        if (display != null) b.mDensity = display.densityDpi;
        return b;
    }

    public static Bitmap createBitmap(int[] colors, int offset, int stride, int width, int height, Config config) {
        Bitmap b = createBitmap(width, height, config);
        b.setPixels(colors, offset, stride, 0, 0, width, height);
        b.mMutable = false;
        return b;
    }

    public static Bitmap createBitmap(int[] colors, int width, int height, Config config) {
        return createBitmap(colors, 0, width, width, height, config);
    }

    public static Bitmap createBitmap(Bitmap src) {
        return createBitmap(src, 0, 0, src.getWidth(), src.getHeight());
    }

    public static Bitmap createBitmap(Bitmap source, int x, int y, int width, int height) {
        return createBitmap(source, x, y, width, height, null, false);
    }

    public static Bitmap createBitmap(Bitmap source, int x, int y, int width, int height, Matrix m, boolean filter) {
        source.checkRecycled("Can't create a bitmap from a recycled bitmap");
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > source.mWidth || y + height > source.mHeight) {
            throw new IllegalArgumentException("x + width must be <= bitmap.width() and y + height <= height()");
        }
        if (m == null || m.isIdentity()) {
            if (x == 0 && y == 0 && width == source.mWidth && height == source.mHeight && !source.mMutable) {
                return source;
            }
            Bitmap b = new Bitmap(nativeCopy(source.mNativePtr, x, y, width, height, width, height, filter), width,
                    height, source.mConfig, false);
            b.mDensity = source.mDensity;
            return b;
        }
        if (m.rectStaysRect() && m.m[Matrix.MSKEW_X] == 0 && m.m[Matrix.MSKEW_Y] == 0
                && m.m[Matrix.MSCALE_X] > 0 && m.m[Matrix.MSCALE_Y] > 0) {
            int dw = Math.max(1, Math.round(width * m.m[Matrix.MSCALE_X]));
            int dh = Math.max(1, Math.round(height * m.m[Matrix.MSCALE_Y]));
            Bitmap b = new Bitmap(nativeCopy(source.mNativePtr, x, y, width, height, dw, dh, filter), dw, dh,
                    source.mConfig, false);
            b.mDensity = source.mDensity;
            return b;
        }
        // General transform: draw through a canvas.
        RectF dst = new RectF(0, 0, width, height);
        m.mapRect(dst);
        int dw = Math.max(1, Math.round(dst.width()));
        int dh = Math.max(1, Math.round(dst.height()));
        Bitmap b = createBitmap(dw, dh, source.mConfig, true);
        Canvas c = new Canvas(b);
        c.translate(-dst.left, -dst.top);
        c.concat(m);
        Paint p = new Paint();
        p.setFilterBitmap(filter);
        c.drawBitmap(source, new Rect(x, y, x + width, y + height), new RectF(0, 0, width, height), p);
        b.mDensity = source.mDensity;
        b.mMutable = false;
        return b;
    }

    public static Bitmap createScaledBitmap(Bitmap src, int dstWidth, int dstHeight, boolean filter) {
        if (dstWidth <= 0 || dstHeight <= 0) throw new IllegalArgumentException("width and height must be > 0");
        src.checkRecycled("Can't scale a recycled bitmap");
        Bitmap b = new Bitmap(nativeCopy(src.mNativePtr, 0, 0, src.mWidth, src.mHeight, dstWidth, dstHeight, filter),
                dstWidth, dstHeight, src.mConfig, false);
        b.mDensity = src.mDensity;
        return b;
    }

    public Bitmap copy(Config config, boolean isMutable) {
        checkRecycled("Can't copy a recycled bitmap");
        Bitmap b = new Bitmap(nativeCopy(mNativePtr, 0, 0, mWidth, mHeight, mWidth, mHeight, false), mWidth, mHeight,
                config, isMutable);
        b.mDensity = mDensity;
        return b;
    }

    public Bitmap extractAlpha() {
        return copy(Config.ALPHA_8, true);
    }

    // --- properties --------------------------------------------------------------------

    private void checkRecycled(String message) {
        if (mRecycled) throw new IllegalStateException(message);
    }

    public final int getWidth() {
        return mWidth;
    }

    public final int getHeight() {
        return mHeight;
    }

    public final Config getConfig() {
        return mConfig;
    }

    public final boolean isMutable() {
        return mMutable;
    }

    public final boolean isRecycled() {
        return mRecycled;
    }

    public void recycle() {
        mRecycled = true;
    }

    public final boolean isPremultiplied() {
        return mPremultiplied;
    }

    public final void setPremultiplied(boolean premultiplied) {
        mPremultiplied = premultiplied;
    }

    public final boolean hasAlpha() {
        return nativeHasAlpha(mNativePtr);
    }

    public void setHasAlpha(boolean hasAlpha) {
        nativeSetHasAlpha(mNativePtr, hasAlpha);
    }

    public final boolean hasMipMap() {
        return false;
    }

    public final void setHasMipMap(boolean hasMipMap) {}

    public int getDensity() {
        return mDensity;
    }

    public void setDensity(int density) {
        mDensity = density;
    }

    public int getScaledWidth(Canvas canvas) {
        return scaleFromDensity(mWidth, mDensity, canvas.getDensity());
    }

    public int getScaledHeight(Canvas canvas) {
        return scaleFromDensity(mHeight, mDensity, canvas.getDensity());
    }

    public int getScaledWidth(DisplayMetrics metrics) {
        return scaleFromDensity(mWidth, mDensity, metrics.densityDpi);
    }

    public int getScaledHeight(DisplayMetrics metrics) {
        return scaleFromDensity(mHeight, mDensity, metrics.densityDpi);
    }

    public int getScaledWidth(int targetDensity) {
        return scaleFromDensity(mWidth, mDensity, targetDensity);
    }

    public int getScaledHeight(int targetDensity) {
        return scaleFromDensity(mHeight, mDensity, targetDensity);
    }

    static int scaleFromDensity(int size, int sdensity, int tdensity) {
        if (sdensity == DENSITY_NONE || tdensity == DENSITY_NONE || sdensity == tdensity) return size;
        return ((size * tdensity) + (sdensity >> 1)) / sdensity;
    }

    public final int getRowBytes() {
        return mWidth * 4;
    }

    public final int getByteCount() {
        return getRowBytes() * mHeight;
    }

    public final int getAllocationByteCount() {
        return getByteCount();
    }

    public int getGenerationId() {
        return (int) nativeGeneration(mNativePtr);
    }

    public byte[] getNinePatchChunk() {
        return mNinePatchChunk;
    }

    public void prepareToDraw() {}

    public boolean sameAs(Bitmap other) {
        if (this == other) return true;
        if (other == null || mWidth != other.mWidth || mHeight != other.mHeight) return false;
        int[] a = new int[mWidth * mHeight];
        int[] b = new int[mWidth * mHeight];
        getPixels(a, 0, mWidth, 0, 0, mWidth, mHeight);
        other.getPixels(b, 0, mWidth, 0, 0, mWidth, mHeight);
        return java.util.Arrays.equals(a, b);
    }

    // --- pixels ----------------------------------------------------------------------------

    private void checkPixelAccess(int x, int y) {
        if (x < 0 || x >= mWidth) throw new IllegalArgumentException("x must be >= 0 and < bitmap.width()");
        if (y < 0 || y >= mHeight) throw new IllegalArgumentException("y must be >= 0 and < bitmap.height()");
    }

    private void checkPixelsAccess(int x, int y, int width, int height, int offset, int stride, int[] pixels) {
        if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > mWidth || y + height > mHeight) {
            throw new IllegalArgumentException("x + width must be <= bitmap.width() and y + height <= height()");
        }
        if (Math.abs(stride) < width) throw new IllegalArgumentException("abs(stride) must be >= width");
        if (width == 0 || height == 0) return;
        int lastScanline = offset + (height - 1) * stride;
        if (offset < 0 || offset + width > pixels.length || lastScanline < 0 || lastScanline + width > pixels.length) {
            throw new ArrayIndexOutOfBoundsException();
        }
    }

    public int getPixel(int x, int y) {
        checkRecycled("Can't call getPixel() on a recycled bitmap");
        checkPixelAccess(x, y);
        int[] out = new int[1];
        nativeGetPixels(mNativePtr, out, 0, 1, x, y, 1, 1);
        return out[0];
    }

    public void getPixels(int[] pixels, int offset, int stride, int x, int y, int width, int height) {
        checkRecycled("Can't call getPixels() on a recycled bitmap");
        if (width == 0 || height == 0) return;
        checkPixelsAccess(x, y, width, height, offset, stride, pixels);
        nativeGetPixels(mNativePtr, pixels, offset, stride, x, y, width, height);
    }

    public void setPixel(int x, int y, int color) {
        checkRecycled("Can't call setPixel() on a recycled bitmap");
        if (!mMutable) throw new IllegalStateException();
        checkPixelAccess(x, y);
        nativeSetPixels(mNativePtr, new int[] {color}, 0, 1, x, y, 1, 1);
    }

    public void setPixels(int[] pixels, int offset, int stride, int x, int y, int width, int height) {
        checkRecycled("Can't call setPixels() on a recycled bitmap");
        if (!mMutable) throw new IllegalStateException();
        if (width == 0 || height == 0) return;
        checkPixelsAccess(x, y, width, height, offset, stride, pixels);
        nativeSetPixels(mNativePtr, pixels, offset, stride, x, y, width, height);
    }

    public void eraseColor(int c) {
        checkRecycled("Can't erase a recycled bitmap");
        if (!mMutable) throw new IllegalStateException("cannot erase immutable bitmaps");
        nativeErase(mNativePtr, c);
    }

    public boolean compress(CompressFormat format, int quality, OutputStream stream) {
        checkRecycled("Can't compress a recycled bitmap");
        if (stream == null) throw new NullPointerException();
        if (quality < 0 || quality > 100) throw new IllegalArgumentException("quality must be 0..100");
        // WebP encoding is not available from WIC: PNG keeps the data lossless.
        int host = format == CompressFormat.JPEG ? 0 : 1;
        byte[] data = nativeCompress(mNativePtr, host, quality);
        if (data == null) return false;
        try {
            stream.write(data);
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    @Override
    public String toString() {
        return "Bitmap{" + mWidth + "x" + mHeight + " " + mConfig + "}";
    }

    static native long nativeCreate(int width, int height, boolean hasAlpha);

    static native long nativeCopy(long src, int x, int y, int width, int height, int dstWidth, int dstHeight,
            boolean filter);

    static native void nativeRegisterCleanup(Bitmap owner, long nativePtr, long bytes);

    static native void nativeGetPixels(long ptr, int[] pixels, int offset, int stride, int x, int y, int width,
            int height);

    static native void nativeSetPixels(long ptr, int[] pixels, int offset, int stride, int x, int y, int width,
            int height);

    static native void nativeErase(long ptr, int color);

    static native boolean nativeHasAlpha(long ptr);

    static native void nativeSetHasAlpha(long ptr, boolean hasAlpha);

    static native long nativeGeneration(long ptr);

    static native byte[] nativeCompress(long ptr, int format, int quality);
}
