package android.graphics;

public class LinearGradient extends Shader {
    private final float mX0, mY0, mX1, mY1;
    private final int[] mColors;
    private final float[] mPositions;
    private final TileMode mTileMode;

    public LinearGradient(float x0, float y0, float x1, float y1, int[] colors, float[] positions, TileMode tile) {
        if (colors.length < 2) throw new IllegalArgumentException("needs >= 2 number of colors");
        if (positions != null && colors.length != positions.length) {
            throw new IllegalArgumentException("color and position arrays must be of equal length");
        }
        mX0 = x0;
        mY0 = y0;
        mX1 = x1;
        mY1 = y1;
        mColors = colors.clone();
        mPositions = positions != null ? positions.clone() : null;
        mTileMode = tile;
    }

    public LinearGradient(float x0, float y0, float x1, float y1, int color0, int color1, TileMode tile) {
        this(x0, y0, x1, y1, new int[] {color0, color1}, null, tile);
    }

    @Override
    float[] nativeSpec() {
        float[] spec = gradientSpec(1, mTileMode, new float[] {mX0, mY0, mX1, mY1}, mColors, mPositions);
        putMatrix(spec);
        return spec;
    }
}