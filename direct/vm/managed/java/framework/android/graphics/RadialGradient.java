package android.graphics;

public class RadialGradient extends Shader {
    private final float mX, mY, mRadius;
    private final int[] mColors;
    private final float[] mPositions;
    private final TileMode mTileMode;

    public RadialGradient(float centerX, float centerY, float radius, int[] colors, float[] stops, TileMode tileMode) {
        if (radius <= 0) throw new IllegalArgumentException("radius must be > 0");
        if (colors.length < 2) throw new IllegalArgumentException("needs >= 2 number of colors");
        if (stops != null && colors.length != stops.length) {
            throw new IllegalArgumentException("color and position arrays must be of equal length");
        }
        mX = centerX;
        mY = centerY;
        mRadius = radius;
        mColors = colors.clone();
        mPositions = stops != null ? stops.clone() : null;
        mTileMode = tileMode;
    }

    public RadialGradient(float centerX, float centerY, float radius, int centerColor, int edgeColor,
            TileMode tileMode) {
        this(centerX, centerY, radius, new int[] {centerColor, edgeColor}, null, tileMode);
    }

    @Override
    float[] nativeSpec() {
        float[] spec = gradientSpec(2, mTileMode, new float[] {mX, mY, mRadius}, mColors, mPositions);
        putMatrix(spec);
        return spec;
    }
}