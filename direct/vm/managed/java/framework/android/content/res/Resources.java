package android.content.res;

import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.util.AndroidRuntimeException;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Locale;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Application resources: typed values, strings, plurals, arrays, dimensions,
 * colors, drawables and compiled XML, chosen for the current configuration
 * from the resource tables (the application's and the runtime's framework
 * package), with themes.
 */
public class Resources {
    static final String TAG = "Resources";

    public static final int ID_NULL = 0;

    public static class NotFoundException extends AndroidRuntimeException {
        public NotFoundException() {}

        public NotFoundException(String name) {
            super(name);
        }

        public NotFoundException(String name, Exception cause) {
            super(name, cause);
        }
    }

    // Bag keys (ResTable_map): plurals and arrays.
    static final int ATTR_OTHER = 0x01000004;
    static final int ATTR_ZERO = 0x01000005;
    static final int ATTR_ONE = 0x01000006;
    static final int ATTR_TWO = 0x01000007;
    static final int ATTR_FEW = 0x01000008;
    static final int ATTR_MANY = 0x01000009;

    private static Resources sSystem;

    final AssetManager mAssets;
    private final DisplayMetrics mMetrics = new DisplayMetrics();
    private final Configuration mConfiguration = new Configuration();
    private final HashMap<String, Drawable.ConstantState> mDrawableCache = new HashMap<String, Drawable.ConstantState>();
    private final HashMap<String, ColorStateList> mColorStateListCache = new HashMap<String, ColorStateList>();
    private final TypedValue mTmpValue = new TypedValue();

    private static final java.util.ArrayList<Resources> sDisplayResources = new java.util.ArrayList<Resources>();

    public Resources(AssetManager assets, DisplayMetrics metrics, Configuration config) {
        mAssets = assets != null ? assets : AssetManager.getSystem();
        updateConfiguration(config, metrics);
    }

    /**
     * Registers resources that follow the host display (the system and the
     * application's); they are updated when the host window is resized.
     */
    public static void trackDisplay(Resources res) {
        synchronized (sDisplayResources) {
            if (!sDisplayResources.contains(res)) sDisplayResources.add(res);
        }
    }

    /** The host window's size changed: new metrics and screen configuration (no activity restarts). */
    public static void onDisplaySizeChanged(int width, int height) {
        Resources[] all;
        synchronized (sDisplayResources) {
            all = sDisplayResources.toArray(new Resources[0]);
        }
        DisplayMetrics metrics = ResourcesHost.displayMetrics();
        Configuration config = ResourcesHost.configuration();
        for (Resources r : all) {
            Configuration c = new Configuration(r.mConfiguration);
            c.screenWidthDp = config.screenWidthDp;
            c.screenHeightDp = config.screenHeightDp;
            c.smallestScreenWidthDp = config.smallestScreenWidthDp;
            c.orientation = config.orientation;
            c.screenLayout = config.screenLayout;
            r.updateConfiguration(c, metrics);
        }
    }

    public static Resources getSystem() {
        synchronized (Resources.class) {
            if (sSystem == null) {
                sSystem = new Resources(AssetManager.getSystem(), ResourcesHost.displayMetrics(),
                        ResourcesHost.configuration());
                trackDisplay(sSystem);
            }
            return sSystem;
        }
    }

    /** The theme a context gets when it names none (Resources.selectDefaultTheme). */
    public static int selectDefaultTheme(int curTheme, int targetSdkVersion) {
        if (curTheme != 0) return curTheme;
        if (targetSdkVersion < 11) return android.R.style.Theme;
        if (targetSdkVersion < 14) return android.R.style.Theme_Holo;
        if (targetSdkVersion < 24) return android.R.style.Theme_DeviceDefault;
        return android.R.style.Theme_DeviceDefault_Light_DarkActionBar;
    }

    public final AssetManager getAssets() {
        return mAssets;
    }

    public DisplayMetrics getDisplayMetrics() {
        return mMetrics;
    }

    public Configuration getConfiguration() {
        return mConfiguration;
    }

    public void updateConfiguration(Configuration config, DisplayMetrics metrics) {
        if (metrics != null) mMetrics.setTo(metrics); else mMetrics.setToDefaults();
        if (config != null) mConfiguration.setTo(config);
        if (mConfiguration.densityDpi == 0) mConfiguration.densityDpi = mMetrics.densityDpi;
        Locale l = mConfiguration.locale != null ? mConfiguration.locale : Locale.getDefault();
        String locale = l.getLanguage() + (l.getCountry().length() > 0 ? "-" + l.getCountry() : "");
        int keysHidden = 1;  // KEYBOARDHIDDEN_NO: a hardware keyboard is present
        AssetManager.setConfiguration(0, 0, locale, mConfiguration.orientation, mConfiguration.touchscreen,
                mMetrics.densityDpi, mConfiguration.keyboard, keysHidden, mConfiguration.navigation,
                Math.max(mMetrics.widthPixels, mMetrics.heightPixels),
                Math.min(mMetrics.widthPixels, mMetrics.heightPixels), mConfiguration.smallestScreenWidthDp,
                mConfiguration.screenWidthDp, mConfiguration.screenHeightDp, mConfiguration.screenLayout,
                mConfiguration.uiMode, 0, android.os.Build.VERSION.SDK_INT);
        synchronized (mDrawableCache) {
            mDrawableCache.clear();
            mColorStateListCache.clear();
        }
    }

    public final Theme newTheme() {
        return new Theme();
    }

    // --- values -----------------------------------------------------------------------

    private NotFoundException notFound(String what, int id) {
        return new NotFoundException(what + " resource ID #0x" + Integer.toHexString(id) + describe(id));
    }

    private String describe(int id) {
        String name = id != 0 ? mAssets.getResourceName(id) : null;
        if (name != null) return " (" + name + ")";
        if ((id >>> 24) == 0x01) return " (a framework resource this runtime does not provide)";
        return "";
    }

    public void getValue(int id, TypedValue outValue, boolean resolveRefs) throws NotFoundException {
        if (!mAssets.getResourceValue(id, 0, outValue, resolveRefs)) throw notFound("", id);
    }

    public void getValueForDensity(int id, int density, TypedValue outValue, boolean resolveRefs)
            throws NotFoundException {
        if (!mAssets.getResourceValue(id, density, outValue, resolveRefs)) throw notFound("", id);
    }

    public void getValue(String name, TypedValue outValue, boolean resolveRefs) throws NotFoundException {
        int id = getIdentifier(name, "string", null);
        if (id != 0) {
            getValue(id, outValue, resolveRefs);
            return;
        }
        throw new NotFoundException("String resource name " + name);
    }

    private TypedValue obtainTempTypedValue() {
        return new TypedValue();
    }

    public CharSequence getText(int id) throws NotFoundException {
        TypedValue v = obtainTempTypedValue();
        getValue(id, v, true);
        if (v.type == TypedValue.TYPE_STRING && v.string != null) return v.string;
        throw new NotFoundException("String resource ID #0x" + Integer.toHexString(id) + describe(id));
    }

    public CharSequence getText(int id, CharSequence def) {
        TypedValue v = obtainTempTypedValue();
        if (id != 0 && mAssets.getResourceValue(id, 0, v, true) && v.type == TypedValue.TYPE_STRING
                && v.string != null) {
            return v.string;
        }
        return def;
    }

    public String getString(int id) throws NotFoundException {
        return getText(id).toString();
    }

    public String getString(int id, Object... formatArgs) throws NotFoundException {
        final String raw = getString(id);
        return String.format(locale(), raw, formatArgs);
    }

    private Locale locale() {
        return mConfiguration.locale != null ? mConfiguration.locale : Locale.getDefault();
    }

    public CharSequence getQuantityText(int id, int quantity) throws NotFoundException {
        int[] bag = mAssets.getBag(id);
        if (bag == null) throw notFound("Plural", id);
        int key = PluralRules.quantityKey(locale(), quantity);
        CharSequence s = bagString(bag, key);
        if (s == null && key != ATTR_OTHER) s = bagString(bag, ATTR_OTHER);
        if (s == null) {
            throw new NotFoundException("Plural resource ID #0x" + Integer.toHexString(id) + describe(id)
                    + " quantity=" + quantity + " item=" + PluralRules.keyName(key));
        }
        return s;
    }

    private CharSequence bagString(int[] bag, int key) {
        int count = bag[1];
        for (int i = 0; i < count; i++) {
            int off = 2 + i * 4;
            if (bag[off] != key) continue;
            TypedValue v = bagValue(bag, i);
            return v.type == TypedValue.TYPE_STRING ? v.string : v.coerceToString();
        }
        return null;
    }

    /** Bag entry `i` as a value, with references resolved. */
    TypedValue bagValue(int[] bag, int i) {
        int off = 2 + i * 4;
        TypedValue v = new TypedValue();
        v.type = bag[off + 1];
        v.data = bag[off + 2];
        v.assetCookie = bag[off + 3];
        v.string = v.type == TypedValue.TYPE_STRING ? mAssets.getPooledStringForCookie(v.assetCookie, v.data) : null;
        if (v.type == TypedValue.TYPE_REFERENCE && v.data != 0) {
            int ref = v.data;
            if (!mAssets.getResourceValue(ref, 0, v, true)) throw notFound("Array item", ref);
            if (v.resourceId == 0) v.resourceId = ref;
        }
        return v;
    }

    public String getQuantityString(int id, int quantity, Object... formatArgs) throws NotFoundException {
        String raw = getQuantityText(id, quantity).toString();
        return String.format(locale(), raw, formatArgs);
    }

    public String getQuantityString(int id, int quantity) throws NotFoundException {
        return getQuantityText(id, quantity).toString();
    }

    private int[] arrayBag(int id, String what) {
        int[] bag = mAssets.getBag(id);
        if (bag == null) throw notFound(what + " array", id);
        return bag;
    }

    public CharSequence[] getTextArray(int id) throws NotFoundException {
        int[] bag = arrayBag(id, "Text");
        int count = bag[1];
        CharSequence[] out = new CharSequence[count];
        for (int i = 0; i < count; i++) {
            TypedValue v = bagValue(bag, i);
            out[i] = v.type == TypedValue.TYPE_STRING ? v.string : v.coerceToString();
        }
        return out;
    }

    public String[] getStringArray(int id) throws NotFoundException {
        CharSequence[] text = getTextArray(id);
        String[] out = new String[text.length];
        for (int i = 0; i < text.length; i++) out[i] = text[i] != null ? text[i].toString() : null;
        return out;
    }

    public int[] getIntArray(int id) throws NotFoundException {
        int[] bag = arrayBag(id, "Int");
        int count = bag[1];
        int[] out = new int[count];
        for (int i = 0; i < count; i++) out[i] = bagValue(bag, i).data;
        return out;
    }

    public TypedArray obtainTypedArray(int id) throws NotFoundException {
        int[] bag = arrayBag(id, "Typed");
        int count = bag[1];
        TypedArray array = new TypedArray(this, count);
        for (int i = 0; i < count; i++) {
            TypedValue v = bagValue(bag, i);
            int off = i * AssetManager.STYLE_NUM_ENTRIES;
            array.mData[off + AssetManager.STYLE_TYPE] = v.type;
            array.mData[off + AssetManager.STYLE_DATA] = v.data;
            array.mData[off + AssetManager.STYLE_ASSET_COOKIE] = v.assetCookie;
            array.mData[off + AssetManager.STYLE_RESOURCE_ID] = v.resourceId;
            array.mData[off + AssetManager.STYLE_DENSITY] = v.density;
            if (v.type != TypedValue.TYPE_NULL) {
                array.mIndices[++array.mIndices[0]] = i;
            }
        }
        return array;
    }

    public int getColor(int id) throws NotFoundException {
        return getColor(id, null);
    }

    public int getColor(int id, Theme theme) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type >= TypedValue.TYPE_FIRST_INT && value.type <= TypedValue.TYPE_LAST_INT) return value.data;
        if (value.type != TypedValue.TYPE_STRING) {
            throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + describe(id) + " type #0x"
                    + Integer.toHexString(value.type) + " is not valid");
        }
        return loadColorStateList(value, id, theme).getDefaultColor();
    }

    public ColorStateList getColorStateList(int id) throws NotFoundException {
        return getColorStateList(id, null);
    }

    public ColorStateList getColorStateList(int id, Theme theme) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        return loadColorStateList(value, id, theme);
    }

    ColorStateList loadColorStateList(TypedValue value, int id, Theme theme) throws NotFoundException {
        if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            return ColorStateList.valueOf(value.data);
        }
        if (value.type != TypedValue.TYPE_STRING || value.string == null) {
            throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + describe(id)
                    + " is not a color: " + value);
        }
        String file = value.string.toString();
        String key = value.assetCookie + ":" + file;
        if (theme == null) {
            synchronized (mDrawableCache) {
                ColorStateList cached = mColorStateListCache.get(key);
                if (cached != null) return cached;
            }
        }
        if (!file.endsWith(".xml")) {
            throw new NotFoundException("File " + file + " from color state list resource ID #0x"
                    + Integer.toHexString(id));
        }
        try (XmlResourceParser rp = loadXmlResourceParser(file, id, value.assetCookie, "ColorStateList")) {
            ColorStateList csl = ColorStateList.createFromXml(this, rp, theme);
            if (theme == null) {
                synchronized (mDrawableCache) {
                    mColorStateListCache.put(key, csl);
                }
            }
            return csl;
        } catch (XmlPullParserException | IOException e) {
            throw new NotFoundException("File " + file + " from color state list resource ID #0x"
                    + Integer.toHexString(id), e);
        }
    }

    public float getDimension(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimension(value.data, mMetrics);
        throw notTyped(id, value, "dimension");
    }

    public int getDimensionPixelOffset(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_DIMENSION) {
            return TypedValue.complexToDimensionPixelOffset(value.data, mMetrics);
        }
        throw notTyped(id, value, "dimension");
    }

    public int getDimensionPixelSize(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_DIMENSION) {
            return TypedValue.complexToDimensionPixelSize(value.data, mMetrics);
        }
        throw notTyped(id, value, "dimension");
    }

    public float getFraction(int id, int base, int pbase) {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_FRACTION) return TypedValue.complexToFraction(value.data, base, pbase);
        throw notTyped(id, value, "fraction");
    }

    public int getInteger(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type >= TypedValue.TYPE_FIRST_INT && value.type <= TypedValue.TYPE_LAST_INT) return value.data;
        throw notTyped(id, value, "integer");
    }

    public float getFloat(int id) {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_FLOAT) return value.getFloat();
        throw notTyped(id, value, "float");
    }

    public boolean getBoolean(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type >= TypedValue.TYPE_FIRST_INT && value.type <= TypedValue.TYPE_LAST_INT) {
            return value.data != 0;
        }
        throw notTyped(id, value, "boolean");
    }

    private NotFoundException notTyped(int id, TypedValue value, String what) {
        return new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + describe(id) + " type #0x"
                + Integer.toHexString(value.type) + " is not valid (expected a " + what + ")");
    }

    // --- drawables -------------------------------------------------------------------------

    @Deprecated
    public Drawable getDrawable(int id) throws NotFoundException {
        return getDrawable(id, null);
    }

    public Drawable getDrawable(int id, Theme theme) throws NotFoundException {
        return getDrawableForDensity(id, 0, theme);
    }

    public Drawable getDrawableForDensity(int id, int density) throws NotFoundException {
        return getDrawableForDensity(id, density, null);
    }

    public Drawable getDrawableForDensity(int id, int density, Theme theme) {
        TypedValue value = obtainTempTypedValue();
        getValueForDensity(id, density, value, true);
        return loadDrawable(value, id, density, theme);
    }

    /** Hidden: creates the drawable a resolved value describes. */
    public Drawable loadDrawable(TypedValue value, int id, int density, Theme theme) throws NotFoundException {
        if (value.type >= TypedValue.TYPE_FIRST_COLOR_INT && value.type <= TypedValue.TYPE_LAST_COLOR_INT) {
            return new ColorDrawable(value.data);
        }
        if (value.type != TypedValue.TYPE_STRING || value.string == null) {
            throw new NotFoundException("Resource \"" + getResourceNameOrId(id) + "\" is not a Drawable (color or path): "
                    + value);
        }
        final String file = value.string.toString();
        final String key = value.assetCookie + ":" + file + ":" + density;
        Drawable.ConstantState cs;
        synchronized (mDrawableCache) {
            cs = mDrawableCache.get(key);
        }
        if (cs != null) {
            Drawable d = cs.newDrawable(this, theme);
            return d;
        }
        Drawable dr;
        try {
            if (file.endsWith(".xml")) {
                try (XmlResourceParser rp = loadXmlResourceParser(file, id, value.assetCookie, "drawable")) {
                    dr = Drawable.createFromXmlForDensity(this, rp, density, theme);
                }
            } else {
                byte[] bytes = mAssets.readNonAsset(value.assetCookie, file);
                dr = Drawable.createFromResourceBytes(this, value, bytes, file);
            }
        } catch (NotFoundException e) {
            throw e;
        } catch (Exception e) {
            NotFoundException nfe = new NotFoundException("File " + file + " from drawable resource ID #0x"
                    + Integer.toHexString(id) + describe(id));
            nfe.initCause(e);
            throw nfe;
        }
        if (dr == null) throw new NotFoundException("File " + file + " is not a drawable");
        if (!file.endsWith(".xml")) {
            Drawable.ConstantState state = dr.getConstantState();
            if (state != null) {
                synchronized (mDrawableCache) {
                    if (mDrawableCache.size() > 256) mDrawableCache.clear();
                    mDrawableCache.put(key, state);
                }
            }
        }
        return dr;
    }

    private String getResourceNameOrId(int id) {
        String name = id != 0 ? mAssets.getResourceName(id) : null;
        return name != null ? name : "#0x" + Integer.toHexString(id);
    }

    // --- XML -----------------------------------------------------------------------------

    public XmlResourceParser getLayout(int id) throws NotFoundException {
        return loadXmlResourceParser(id, "layout");
    }

    public XmlResourceParser getAnimation(int id) throws NotFoundException {
        return loadXmlResourceParser(id, "anim");
    }

    public XmlResourceParser getXml(int id) throws NotFoundException {
        return loadXmlResourceParser(id, "xml");
    }

    XmlResourceParser loadXmlResourceParser(int id, String type) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        getValue(id, value, true);
        if (value.type == TypedValue.TYPE_STRING && value.string != null) {
            return loadXmlResourceParser(value.string.toString(), id, value.assetCookie, type);
        }
        throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + describe(id) + " type #0x"
                + Integer.toHexString(value.type) + " is not valid");
    }

    /** Hidden: a parser over compiled XML file `file` of package `cookie`. */
    public XmlResourceParser loadXmlResourceParser(String file, int id, int assetCookie, String type)
            throws NotFoundException {
        if (!file.endsWith(".xml")) {
            throw new NotFoundException("File " + file + " from xml type " + type + " resource ID #0x"
                    + Integer.toHexString(id));
        }
        try {
            return mAssets.openXmlBlock(assetCookie, file).newParser();
        } catch (IOException | RuntimeException e) {
            NotFoundException nfe = new NotFoundException("File " + file + " from xml type " + type
                    + " resource ID #0x" + Integer.toHexString(id));
            nfe.initCause(e);
            throw nfe;
        }
    }

    public InputStream openRawResource(int id) throws NotFoundException {
        TypedValue value = obtainTempTypedValue();
        return openRawResource(id, value);
    }

    public InputStream openRawResource(int id, TypedValue value) throws NotFoundException {
        getValue(id, value, true);
        if (value.type != TypedValue.TYPE_STRING || value.string == null) {
            throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + describe(id)
                    + " is not a file");
        }
        try {
            return mAssets.openNonAsset(value.assetCookie, value.string.toString());
        } catch (IOException e) {
            NotFoundException nfe = new NotFoundException("File " + value.string + " from resource ID #0x"
                    + Integer.toHexString(id));
            nfe.initCause(e);
            throw nfe;
        }
    }

    // --- names -------------------------------------------------------------------------------

    public int getIdentifier(String name, String defType, String defPackage) {
        if (name == null) throw new NullPointerException("name is null");
        try {
            return Integer.parseInt(name);
        } catch (Exception e) {
            // not a number: look the name up
        }
        return mAssets.getResourceIdentifier(name, defType, defPackage);
    }

    public String getResourceName(int resid) throws NotFoundException {
        String name = mAssets.getResourceName(resid);
        if (name != null) return name;
        throw new NotFoundException("Unable to find resource ID #0x" + Integer.toHexString(resid));
    }

    public String getResourcePackageName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(0, name.indexOf(':'));
    }

    public String getResourceTypeName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(name.indexOf(':') + 1, name.indexOf('/'));
    }

    public String getResourceEntryName(int resid) throws NotFoundException {
        String name = getResourceName(resid);
        return name.substring(name.indexOf('/') + 1);
    }

    // --- attributes ------------------------------------------------------------------------------

    public TypedArray obtainAttributes(AttributeSet set, int[] attrs) {
        TypedArray array = new TypedArray(this, attrs.length);
        XmlBlock.Parser parser = set instanceof XmlBlock.Parser ? (XmlBlock.Parser) set : null;
        int[][] xml = xmlArrays(parser);
        AssetManager.retrieveAttributes(xml[0], xml[1], xml[2], attrs, array.mData, array.mIndices);
        array.mXml = parser;
        return array;
    }

    static int[][] xmlArrays(XmlBlock.Parser parser) {
        if (parser == null || parser.getAttributeCount() <= 0) return new int[][] {null, null, null};
        int n = parser.getAttributeCount();
        int[] ids = new int[n], types = new int[n], data = new int[n];
        parser.fillAttributeArrays(ids, types, data);
        return new int[][] {ids, types, data};
    }

    public void flushLayoutCache() {}

    /** A theme: styles applied in order over this Resources' tables. */
    public final class Theme {
        private int[] mStyles = new int[0];
        private int mThemeId = -1;

        Theme() {}

        int nativeTheme() {
            if (mThemeId < 0) mThemeId = AssetManager.createTheme(mStyles);
            return mThemeId;
        }

        public void applyStyle(int resId, boolean force) {
            if (resId == 0) return;
            int[] next = new int[mStyles.length + 2];
            System.arraycopy(mStyles, 0, next, 0, mStyles.length);
            next[mStyles.length] = resId;
            next[mStyles.length + 1] = force ? 1 : 0;
            mStyles = next;
            mThemeId = -1;
        }

        public void setTo(Theme other) {
            mStyles = other.mStyles.clone();
            mThemeId = other.mThemeId;
        }

        public void rebase() {
            mThemeId = -1;
        }

        public TypedArray obtainStyledAttributes(int[] attrs) {
            return obtainStyledAttributes(null, attrs, 0, 0);
        }

        public TypedArray obtainStyledAttributes(int resId, int[] attrs) throws NotFoundException {
            return obtainStyledAttributes(null, attrs, 0, resId);
        }

        public TypedArray obtainStyledAttributes(AttributeSet set, int[] attrs, int defStyleAttr, int defStyleRes) {
            TypedArray array = new TypedArray(Resources.this, attrs.length);
            XmlBlock.Parser parser = set instanceof XmlBlock.Parser ? (XmlBlock.Parser) set : null;
            int[][] xml = xmlArrays(parser);
            int[] style = parser != null && parser.getAttributeCount() > 0 ? parser.styleAttributeValue()
                    : new int[] {TypedValue.TYPE_NULL, 0};
            AssetManager.applyStyle(nativeTheme(), xml[0], xml[1], xml[2], style[0], style[1], defStyleAttr,
                    defStyleRes, attrs, array.mData, array.mIndices);
            array.mTheme = this;
            array.mXml = parser;
            return array;
        }

        public TypedArray resolveAttributes(int[] values, int[] attrs) {
            return obtainStyledAttributes(null, attrs, 0, 0);
        }

        public boolean resolveAttribute(int resid, TypedValue outValue, boolean resolveRefs) {
            return mAssets.getThemeValue(nativeTheme(), resid, outValue, resolveRefs);
        }

        public Resources getResources() {
            return Resources.this;
        }

        public Drawable getDrawable(int id) throws NotFoundException {
            return Resources.this.getDrawable(id, this);
        }

        public int getChangingConfigurations() {
            return 0;
        }

        /** Hidden: the style resources applied to this theme, in order. */
        public int[] getAppliedStyles() {
            int[] out = new int[mStyles.length / 2];
            for (int i = 0; i < out.length; i++) out[i] = mStyles[i * 2];
            return out;
        }

        public void dump(int priority, String tag, String prefix) {
            for (int i = 0; i < mStyles.length; i += 2) {
                Log.println(priority, tag, prefix + getResourceNameOrId(mStyles[i]) + (mStyles[i + 1] != 0
                        ? " (forced)" : ""));
            }
        }

        @Override
        public String toString() {
            StringBuilder sb = new StringBuilder("Theme{");
            for (int i = 0; i < mStyles.length; i += 2) {
                if (i > 0) sb.append(", ");
                sb.append(getResourceNameOrId(mStyles[i]));
            }
            return sb.append('}').toString();
        }
    }
}
