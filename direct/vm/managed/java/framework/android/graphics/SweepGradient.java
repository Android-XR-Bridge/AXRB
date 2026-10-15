package android.graphics;

/** A sweep gradient. The renderer approximates it with its first color. */
public class SweepGradient extends Shader {
    final int mFirstColor;

    public SweepGradient(float cx, float cy, int[] colors, float[] positions) {
        if (colors.length < 2) throw new IllegalArgumentException("needs >= 2 number of colors");
        mFirstColor = colors[0];
    }

    public SweepGradient(float cx, float cy, int color0, int color1) {
        mFirstColor = color0;
    }
}