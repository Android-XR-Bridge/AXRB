package android.hardware.camera2.params;

public final class MeteringRectangle {
    public static final int METERING_WEIGHT_MAX = 1000;

    private final int mX, mY, mWidth, mHeight, mWeight;

    public MeteringRectangle(int x, int y, int width, int height, int meteringWeight) {
        mX = x;
        mY = y;
        mWidth = width;
        mHeight = height;
        mWeight = meteringWeight;
    }

    public int getX() {
        return mX;
    }

    public int getY() {
        return mY;
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public int getMeteringWeight() {
        return mWeight;
    }
}
