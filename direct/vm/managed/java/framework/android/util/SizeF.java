package android.util;

public final class SizeF {
    private final float mWidth;
    private final float mHeight;

    public SizeF(float width, float height) {
        mWidth = width;
        mHeight = height;
    }

    public float getWidth() {
        return mWidth;
    }

    public float getHeight() {
        return mHeight;
    }

    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj instanceof SizeF) {
            SizeF other = (SizeF) obj;
            return mWidth == other.mWidth && mHeight == other.mHeight;
        }
        return false;
    }

    @Override
    public String toString() {
        return mWidth + "x" + mHeight;
    }

    @Override
    public int hashCode() {
        return Float.floatToIntBits(mWidth) ^ Float.floatToIntBits(mHeight);
    }
}
