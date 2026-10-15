package android.graphics;

public class LightingColorFilter extends ColorFilter {
    private final int mMul, mAdd;

    public LightingColorFilter(int mul, int add) {
        mMul = mul;
        mAdd = add;
    }

    @Override
    int filter(int argb) {
        int r = Math.min(255, (((argb >> 16) & 0xff) * ((mMul >> 16) & 0xff)) / 255 + ((mAdd >> 16) & 0xff));
        int g = Math.min(255, (((argb >> 8) & 0xff) * ((mMul >> 8) & 0xff)) / 255 + ((mAdd >> 8) & 0xff));
        int b = Math.min(255, ((argb & 0xff) * (mMul & 0xff)) / 255 + (mAdd & 0xff));
        return (argb & 0xff000000) | (r << 16) | (g << 8) | b;
    }
}