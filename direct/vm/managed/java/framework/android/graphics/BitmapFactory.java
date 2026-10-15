package android.graphics;

import android.content.res.AssetManager;
import android.content.res.Resources;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Decodes images with the host's codecs (WIC: PNG, JPEG, GIF, BMP, ICO, TIFF,
 * and WebP where the Windows codec is installed), applying resource density
 * scaling like Android.
 */
public class BitmapFactory {
    public static class Options {
        public Bitmap inBitmap;
        public boolean inMutable;
        public boolean inJustDecodeBounds;
        public int inSampleSize;
        public Bitmap.Config inPreferredConfig = Bitmap.Config.ARGB_8888;
        public boolean inPremultiplied = true;
        public boolean inDither;
        public int inDensity;
        public int inTargetDensity;
        public int inScreenDensity;
        public boolean inScaled = true;
        public boolean inPurgeable;
        public boolean inInputShareable;
        public boolean inPreferQualityOverSpeed;
        public int outWidth;
        public int outHeight;
        public String outMimeType;
        public Bitmap.Config outConfig;
        public byte[] inTempStorage;
        public boolean mCancel;

        public void requestCancelDecode() {
            mCancel = true;
        }
    }

    public static Bitmap decodeResource(Resources res, int id, Options opts) {
        TypedValue value = new TypedValue();
        try (InputStream is = res.openRawResource(id, value)) {
            return decodeResourceStream(res, value, is, null, opts);
        } catch (IOException e) {
            return null;
        } catch (Resources.NotFoundException e) {
            return null;
        }
    }

    public static Bitmap decodeResource(Resources res, int id) {
        return decodeResource(res, id, null);
    }

    public static Bitmap decodeResourceStream(Resources res, TypedValue value, InputStream is, Rect pad, Options opts) {
        if (opts == null) opts = new Options();
        if (opts.inDensity == 0 && value != null) {
            final int density = value.density;
            if (density == TypedValue.DENSITY_DEFAULT) {
                opts.inDensity = DisplayMetrics.DENSITY_DEFAULT;
            } else if (density != TypedValue.DENSITY_NONE) {
                opts.inDensity = density;
            }
        }
        if (opts.inTargetDensity == 0 && res != null) opts.inTargetDensity = res.getDisplayMetrics().densityDpi;
        return decodeStream(is, pad, opts);
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length, Options opts) {
        if ((offset | length) < 0 || data.length < offset + length) throw new ArrayIndexOutOfBoundsException();
        return decode(data, offset, length, opts);
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length) {
        return decodeByteArray(data, offset, length, null);
    }

    public static Bitmap decodeStream(InputStream is, Rect outPadding, Options opts) {
        if (is == null) return null;
        byte[] data;
        try {
            data = readAll(is);
        } catch (IOException e) {
            return null;
        }
        Bitmap bm = decode(data, 0, data.length, opts);
        if (bm != null && outPadding != null && bm.mNinePatchChunk != null) {
            NinePatch.readPadding(bm.mNinePatchChunk, outPadding);
        }
        return bm;
    }

    public static Bitmap decodeStream(InputStream is) {
        return decodeStream(is, null, null);
    }

    public static Bitmap decodeFile(String pathName, Options opts) {
        try (FileInputStream in = new FileInputStream(pathName)) {
            return decodeStream(in, null, opts);
        } catch (IOException e) {
            return null;
        }
    }

    public static Bitmap decodeFile(String pathName) {
        return decodeFile(pathName, null);
    }

    static byte[] readAll(InputStream is) throws IOException {
        if (is instanceof AssetManager.AssetInputStream) {
            byte[] data = new byte[is.available()];
            int n = 0;
            while (n < data.length) {
                int r = is.read(data, n, data.length - n);
                if (r < 0) break;
                n += r;
            }
            return data;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int r;
        while ((r = is.read(buf)) > 0) out.write(buf, 0, r);
        return out.toByteArray();
    }

    private static String mimeType(byte[] d, int off, int len) {
        if (len >= 8 && (d[off] & 0xff) == 0x89 && d[off + 1] == 'P' && d[off + 2] == 'N' && d[off + 3] == 'G') {
            return "image/png";
        }
        if (len >= 3 && (d[off] & 0xff) == 0xFF && (d[off + 1] & 0xff) == 0xD8) return "image/jpeg";
        if (len >= 6 && d[off] == 'G' && d[off + 1] == 'I' && d[off + 2] == 'F') return "image/gif";
        if (len >= 2 && d[off] == 'B' && d[off + 1] == 'M') return "image/bmp";
        if (len >= 12 && d[off] == 'R' && d[off + 1] == 'I' && d[off + 2] == 'F' && d[off + 3] == 'F'
                && d[off + 8] == 'W' && d[off + 9] == 'E' && d[off + 10] == 'B' && d[off + 11] == 'P') {
            return "image/webp";
        }
        return null;
    }

    private static Bitmap decode(byte[] data, int offset, int length, Options opts) {
        int[] size = new int[2];
        if (!nativeDecodeBounds(data, offset, length, size)) {
            if (opts != null) {
                opts.outWidth = -1;
                opts.outHeight = -1;
                opts.outMimeType = null;
            }
            return null;
        }
        int width = size[0], height = size[1];
        final int decodedWidth = width, decodedHeight = height;
        int sample = opts != null && opts.inSampleSize > 1 ? Integer.highestOneBit(opts.inSampleSize) : 1;
        width = Math.max(1, width / sample);
        height = Math.max(1, height / sample);
        int density = 0;
        int target = 0;
        boolean scaled = false;
        if (opts != null) {
            density = opts.inDensity;
            target = opts.inTargetDensity;
            if (opts.inScaled && density != 0 && target != 0 && density != target && density != opts.inScreenDensity) {
                width = Math.max(1, (int) (width * (float) target / density + 0.5f));
                height = Math.max(1, (int) (height * (float) target / density + 0.5f));
                scaled = true;
            }
            opts.outWidth = width;
            opts.outHeight = height;
            opts.outMimeType = mimeType(data, offset, length);
            opts.outConfig = Bitmap.Config.ARGB_8888;
            if (opts.inJustDecodeBounds) return null;
        }
        long ptr = nativeDecode(data, offset, length, width, height);
        if (ptr == 0) return null;
        Bitmap bm = new Bitmap(ptr, width, height, Bitmap.Config.ARGB_8888, opts != null && opts.inMutable);
        if (scaled) {
            bm.mDensity = target;
        } else if (density != 0) {
            bm.mDensity = density;
        }
        byte[] chunk = NinePatch.findChunk(data, offset, length);
        if (chunk != null && (width != decodedWidth || height != decodedHeight)) {
            chunk = NinePatch.scaleChunk(chunk, width / (float) decodedWidth, height / (float) decodedHeight);
        }
        bm.mNinePatchChunk = chunk;
        return bm;
    }

    static native boolean nativeDecodeBounds(byte[] data, int offset, int length, int[] outSize);

    /** A handle to the decoded image scaled to exactly width x height, or 0. */
    static native long nativeDecode(byte[] data, int offset, int length, int width, int height);
}
