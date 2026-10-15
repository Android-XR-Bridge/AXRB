package android.content.res;

import android.util.TypedValue;
import java.io.ByteArrayInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

/**
 * Access to the process's resource tables and the application's assets/.
 * There is one set of tables per process (the runtime's framework package,
 * cookie 0, and the application's, cookie 1); every AssetManager instance
 * reads that set, which is what an application observes on Android too.
 */
public final class AssetManager implements AutoCloseable {
    public static final int ACCESS_UNKNOWN = 0;
    public static final int ACCESS_RANDOM = 1;
    public static final int ACCESS_STREAMING = 2;
    public static final int ACCESS_BUFFER = 3;

    /** TypedArray / native value layout. */
    static final int STYLE_NUM_ENTRIES = 7;
    static final int STYLE_TYPE = 0;
    static final int STYLE_DATA = 1;
    static final int STYLE_ASSET_COOKIE = 2;
    static final int STYLE_RESOURCE_ID = 3;
    static final int STYLE_CHANGING_CONFIGURATIONS = 4;
    static final int STYLE_DENSITY = 5;
    static final int STYLE_SOURCE_RESOURCE_ID = 6;

    public static final int COOKIE_FRAMEWORK = 0;
    public static final int COOKIE_APP = 1;

    private static AssetManager sSystem;

    private boolean open = true;

    /** Hidden on Android. Every instance reads the process's packages. */
    public AssetManager() {}

    public static AssetManager getSystem() {
        synchronized (AssetManager.class) {
            if (sSystem == null) sSystem = new AssetManager();
            return sSystem;
        }
    }

    // --- assets/ -----------------------------------------------------------------

    public InputStream open(String fileName) throws IOException {
        return open(fileName, ACCESS_STREAMING);
    }

    public InputStream open(String fileName, int accessMode) throws IOException {
        if (fileName == null) throw new NullPointerException("fileName");
        ensureOpen();
        byte[] data = nativeOpenAsset(fileName);
        if (data == null) throw new FileNotFoundException(fileName);
        return new AssetInputStream(data);
    }

    /** An uncompressed asset as (descriptor of the APK, offset, length). */
    public AssetFileDescriptor openFd(String fileName) throws IOException {
        if (fileName == null) throw new NullPointerException("fileName");
        ensureOpen();
        long[] r = nativeOpenAssetFd(fileName);
        if (r == null) throw new FileNotFoundException(fileName);
        if (r[0] < 0) {
            throw new FileNotFoundException("This file can not be opened as a file descriptor; it is probably compressed");
        }
        return new AssetFileDescriptor(android.os.ParcelFileDescriptor.adoptFd((int) r[0]), r[1], r[2]);
    }

    public AssetFileDescriptor openNonAssetFd(String fileName) throws IOException {
        return openFd(fileName);
    }

    public String[] list(String path) throws IOException {
        if (path == null) throw new NullPointerException("path");
        ensureOpen();
        return nativeList(path);
    }

    /** Hidden: a file of a package other than assets/ (res/drawable/x.png). */
    public InputStream openNonAsset(int cookie, String fileName) throws IOException {
        ensureOpen();
        return new AssetInputStream(nativeOpenNonAsset(cookie, fileName));
    }

    public InputStream openNonAsset(String fileName) throws IOException {
        return openNonAsset(COOKIE_APP, fileName);
    }

    byte[] readNonAsset(int cookie, String fileName) throws IOException {
        ensureOpen();
        return nativeOpenNonAsset(cookie, fileName);
    }

    /** Hidden: parser over the compiled XML file `fileName` of package `cookie`. */
    public XmlResourceParser openXmlResourceParser(int cookie, String fileName) throws IOException {
        return openXmlBlock(cookie, fileName).newParser();
    }

    public XmlResourceParser openXmlResourceParser(String fileName) throws IOException {
        return openXmlResourceParser(COOKIE_APP, fileName);
    }

    XmlBlock openXmlBlock(int cookie, String fileName) throws IOException {
        ensureOpen();
        Object[] parts;
        try {
            parts = nativeOpenXmlBlock(cookie, fileName);
        } catch (RuntimeException e) {
            throw e;
        }
        return new XmlBlock(parts, fileName);
    }

    @Override
    public void close() {
        // Android keeps the system AssetManager open for the process lifetime too.
    }

    private void ensureOpen() {
        if (!open) throw new RuntimeException("AssetManager has been finalized!");
    }

    // --- resource values ---------------------------------------------------------------

    /** Fills `outValue` for `resId`; false if the resource does not exist. */
    boolean getResourceValue(int resId, int densityDpi, TypedValue outValue, boolean resolveRefs) {
        int[] v = new int[STYLE_NUM_ENTRIES];
        if (!nativeGetResourceValue(resId, densityDpi, resolveRefs, v)) return false;
        fillValue(v, 0, outValue);
        return true;
    }

    void fillValue(int[] v, int off, TypedValue outValue) {
        outValue.type = v[off + STYLE_TYPE];
        outValue.data = v[off + STYLE_DATA];
        outValue.assetCookie = v[off + STYLE_ASSET_COOKIE];
        outValue.resourceId = v[off + STYLE_RESOURCE_ID];
        outValue.changingConfigurations = v[off + STYLE_CHANGING_CONFIGURATIONS];
        outValue.density = v[off + STYLE_DENSITY];
        outValue.sourceResourceId = v[off + STYLE_SOURCE_RESOURCE_ID];
        outValue.string = outValue.type == TypedValue.TYPE_STRING
                ? getPooledStringForCookie(outValue.assetCookie, outValue.data) : null;
    }

    CharSequence getPooledStringForCookie(int cookie, int index) {
        return cookie < 0 ? null : nativeGetPooledString(cookie, index);
    }

    /** Hidden: [specFlags, count, (key, type, data, cookie)*] or null if not a bag. */
    int[] getBag(int resId) {
        return nativeGetBag(resId);
    }

    String getResourceName(int resId) {
        return nativeGetResourceName(resId);
    }

    int getResourceIdentifier(String name, String defType, String defPackage) {
        return nativeGetIdentifier(name, defType, defPackage);
    }

    static int createTheme(int[] styles) {
        return nativeThemeCreate(styles);
    }

    boolean getThemeValue(int theme, int attr, TypedValue outValue, boolean resolveRefs) {
        int[] v = new int[STYLE_NUM_ENTRIES];
        if (!nativeThemeGetAttribute(theme, attr, resolveRefs, v)) return false;
        fillValue(v, 0, outValue);
        return true;
    }

    static void applyStyle(int theme, int[] xmlIds, int[] xmlTypes, int[] xmlData, int styleType, int styleData,
            int defStyleAttr, int defStyleRes, int[] attrs, int[] outData, int[] outIndices) {
        nativeApplyStyle(theme, xmlIds, xmlTypes, xmlData, styleType, styleData, defStyleAttr, defStyleRes, attrs,
                outData, outIndices);
    }

    static void retrieveAttributes(int[] xmlIds, int[] xmlTypes, int[] xmlData, int[] attrs, int[] outData,
            int[] outIndices) {
        nativeRetrieveAttributes(xmlIds, xmlTypes, xmlData, attrs, outData, outIndices);
    }

    static void setConfiguration(int mcc, int mnc, String locale, int orientation, int touchscreen, int density,
            int keyboard, int keyboardHidden, int navigation, int screenWidth, int screenHeight,
            int smallestScreenWidthDp, int screenWidthDp, int screenHeightDp, int screenLayout, int uiMode,
            int colorMode, int majorVersion) {
        nativeSetConfiguration(mcc, mnc, locale, orientation, touchscreen, density, keyboard, keyboardHidden,
                navigation, screenWidth, screenHeight, smallestScreenWidthDp, screenWidthDp, screenHeightDp,
                screenLayout, uiMode, colorMode, majorVersion);
    }

    public final class AssetInputStream extends InputStream {
        private final ByteArrayInputStream in;

        AssetInputStream(byte[] data) {
            in = new ByteArrayInputStream(data);
        }

        @Override
        public int read() {
            return in.read();
        }

        @Override
        public int read(byte[] b, int off, int len) {
            return in.read(b, off, len);
        }

        @Override
        public long skip(long n) {
            return in.skip(n);
        }

        @Override
        public int available() {
            return in.available();
        }

        @Override
        public boolean markSupported() {
            return true;
        }

        @Override
        public void mark(int readlimit) {
            in.mark(readlimit);
        }

        @Override
        public void reset() {
            in.reset();
        }

        @Override
        public void close() {}
    }

    /** The entry assets/<name> of the application APK, or null if absent. */
    private static native byte[] nativeOpenAsset(String name);

    private static native String[] nativeList(String path);

    /** {fd, offset, length}; fd -1 if the asset is compressed; null if absent. */
    private static native long[] nativeOpenAssetFd(String name);

    private static native void nativeSetConfiguration(int mcc, int mnc, String locale, int orientation,
            int touchscreen, int density, int keyboard, int keyboardHidden, int navigation, int screenWidth,
            int screenHeight, int smallestScreenWidthDp, int screenWidthDp, int screenHeightDp, int screenLayout,
            int uiMode, int colorMode, int majorVersion);

    private static native boolean nativeGetResourceValue(int resId, int density, boolean resolve, int[] out);

    private static native int[] nativeGetBag(int resId);

    private static native String nativeGetResourceName(int resId);

    private static native int nativeGetIdentifier(String name, String defType, String defPackage);

    private static native String nativeGetPooledString(int cookie, int index);

    private static native int nativeThemeCreate(int[] styles);

    private static native boolean nativeThemeGetAttribute(int theme, int attr, boolean resolve, int[] out);

    private static native void nativeApplyStyle(int theme, int[] xmlIds, int[] xmlTypes, int[] xmlData,
            int styleType, int styleData, int defStyleAttr, int defStyleRes, int[] attrs, int[] outData,
            int[] outIndices);

    private static native void nativeRetrieveAttributes(int[] xmlIds, int[] xmlTypes, int[] xmlData, int[] attrs,
            int[] outData, int[] outIndices);

    private static native byte[] nativeOpenNonAsset(int cookie, String path);

    private static native Object[] nativeOpenXmlBlock(int cookie, String path);
}
