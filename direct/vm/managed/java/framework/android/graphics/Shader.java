package android.graphics;

/** Base class for paint shaders. The renderer receives a packed description. */
public class Shader {
    public enum TileMode {
        CLAMP, REPEAT, MIRROR
    }

    private Matrix mLocalMatrix;

    public Shader() {}

    public boolean getLocalMatrix(Matrix localM) {
        if (mLocalMatrix == null || mLocalMatrix.isIdentity()) return false;
        localM.set(mLocalMatrix);
        return true;
    }

    public void setLocalMatrix(Matrix localM) {
        mLocalMatrix = localM == null || localM.isIdentity() ? null : new Matrix(localM);
    }

    /** Hidden: [kind, tile, n, m0..m5, geometry..., colors, positions], or null (unsupported). */
    float[] nativeSpec() {
        return null;
    }

    final void putMatrix(float[] spec) {
        Matrix m = mLocalMatrix != null ? mLocalMatrix : new Matrix();
        spec[3] = m.m[Matrix.MSCALE_X];
        spec[4] = m.m[Matrix.MSKEW_Y];
        spec[5] = m.m[Matrix.MSKEW_X];
        spec[6] = m.m[Matrix.MSCALE_Y];
        spec[7] = m.m[Matrix.MTRANS_X];
        spec[8] = m.m[Matrix.MTRANS_Y];
    }

    static float[] gradientSpec(int kind, TileMode tile, float[] geometry, int[] colors, float[] positions) {
        int n = colors.length;
        float[] spec = new float[9 + geometry.length + 2 * n];
        spec[0] = kind;
        spec[1] = tile == TileMode.REPEAT ? 1 : tile == TileMode.MIRROR ? 2 : 0;
        spec[2] = n;
        System.arraycopy(geometry, 0, spec, 9, geometry.length);
        int c = 9 + geometry.length;
        for (int i = 0; i < n; i++) {
            spec[c + i] = Float.intBitsToFloat(colors[i]);
            spec[c + n + i] = positions != null ? positions[i] : -1f;
        }
        return spec;
    }
}