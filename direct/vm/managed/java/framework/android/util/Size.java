package android.util;

public final class Size {
    private final int mWidth;
    private final int mHeight;

    public Size(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj instanceof Size) {
            Size other = (Size) obj;
            return mWidth == other.mWidth && mHeight == other.mHeight;
        }
        return false;
    }

    @Override
    public String toString() {
        return mWidth + "x" + mHeight;
    }

    public static Size parseSize(String string) throws NumberFormatException {
        if (string == null) throw new NullPointerException("string must not be null");
        int sep = string.indexOf('*');
        if (sep < 0) sep = string.indexOf('x');
        if (sep < 0) throw new NumberFormatException("Invalid Size: \"" + string + "\"");
        return new Size(Integer.parseInt(string.substring(0, sep)), Integer.parseInt(string.substring(sep + 1)));
    }

    @Override
    public int hashCode() {
        return mHeight ^ ((mWidth << (Integer.SIZE / 2)) | (mWidth >>> (Integer.SIZE / 2)));
    }
}
