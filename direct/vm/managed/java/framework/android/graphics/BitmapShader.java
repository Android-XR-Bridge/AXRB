package android.graphics;

/** A bitmap shader. Not rendered yet: shapes painted with it use the paint color. */
public class BitmapShader extends Shader {
    final Bitmap mBitmap;
    final TileMode mTileX, mTileY;

    public BitmapShader(Bitmap bitmap, TileMode tileX, TileMode tileY) {
        if (bitmap == null) throw new IllegalArgumentException("Bitmap must be non-null");
        mBitmap = bitmap;
        mTileX = tileX;
        mTileY = tileY;
    }
}