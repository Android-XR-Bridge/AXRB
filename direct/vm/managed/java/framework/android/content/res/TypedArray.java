package android.content.res;

import android.graphics.drawable.Drawable;
import android.util.DisplayMetrics;
import android.util.TypedValue;

/**
 * Values retrieved with obtainStyledAttributes/obtainAttributes/obtainTypedArray.
 * Layout: STYLE_NUM_ENTRIES ints per attribute (type, data, cookie, resource
 * id, changing configurations, density, source style); mIndices[0] is the
 * number of attributes with a value, followed by their indices.
 */
public class TypedArray implements AutoCloseable {
    private final Resources mResources;
    private final DisplayMetrics mMetrics;
    private final AssetManager mAssets;
    final int[] mData;
    final int[] mIndices;
    private final int mLength;
    private boolean mRecycled;
    XmlBlock.Parser mXml;
    Resources.Theme mTheme;

    TypedArray(Resources resources, int length) {
        mResources = resources;
        mMetrics = resources.getDisplayMetrics();
        mAssets = resources.getAssets();
        mLength = length;
        mData = new int[length * AssetManager.STYLE_NUM_ENTRIES];
        mIndices = new int[length + 1];
    }

    private void checkRecycled() {
        if (mRecycled) throw new RuntimeException("Cannot make calls to a recycled instance!");
    }

    private int type(int index) {
        return mData[index * AssetManager.STYLE_NUM_ENTRIES + AssetManager.STYLE_TYPE];
    }

    private int data(int index) {
        return mData[index * AssetManager.STYLE_NUM_ENTRIES + AssetManager.STYLE_DATA];
    }

    public int length() {
        checkRecycled();
        return mLength;
    }

    public int getIndexCount() {
        checkRecycled();
        return mIndices[0];
    }

    public int getIndex(int at) {
        checkRecycled();
        return mIndices[1 + at];
    }

    public Resources getResources() {
        checkRecycled();
        return mResources;
    }

    private CharSequence loadStringValueAt(int index) {
        final int off = index * AssetManager.STYLE_NUM_ENTRIES;
        final int cookie = mData[off + AssetManager.STYLE_ASSET_COOKIE];
        if (cookie < 0) return mXml != null ? mXml.getPooledString(mData[off + AssetManager.STYLE_DATA]) : null;
        return mAssets.getPooledStringForCookie(cookie, mData[off + AssetManager.STYLE_DATA]);
    }

    private UnsupportedOperationException unresolvedAttribute(int index) {
        return new UnsupportedOperationException("Failed to resolve attribute at index " + index + ": "
                + valueAt(index));
    }

    public CharSequence getText(int index) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return null;
        if (type == TypedValue.TYPE_STRING) return loadStringValueAt(index);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        return valueAt(index).coerceToString();
    }

    public String getString(int index) {
        CharSequence cs = getText(index);
        return cs != null ? cs.toString() : null;
    }

    public String getNonResourceString(int index) {
        checkRecycled();
        final int off = index * AssetManager.STYLE_NUM_ENTRIES;
        if (type(index) == TypedValue.TYPE_STRING && mData[off + AssetManager.STYLE_ASSET_COOKIE] < 0) {
            CharSequence cs = loadStringValueAt(index);
            return cs != null ? cs.toString() : null;
        }
        return null;
    }

    public boolean getBoolean(int index, boolean defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index) != 0;
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        CharSequence s = valueAt(index).coerceToString();
        return s != null ? "true".equalsIgnoreCase(s.toString()) || "1".equals(s.toString()) : defValue;
    }

    public int getInt(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        CharSequence s = valueAt(index).coerceToString();
        return s != null ? parseInt(s.toString(), defValue) : defValue;
    }

    private static int parseInt(String s, int def) {
        try {
            if (s.startsWith("#")) return (int) Long.parseLong(s.substring(1), 16);
            if (s.startsWith("0x") || s.startsWith("0X")) return (int) Long.parseLong(s.substring(2), 16);
            return Integer.parseInt(s);
        } catch (NumberFormatException e) {
            return def;
        }
    }

    public float getFloat(int index, float defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type == TypedValue.TYPE_FLOAT) return Float.intBitsToFloat(data(index));
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        CharSequence s = valueAt(index).coerceToString();
        if (s != null) {
            try {
                return Float.parseFloat(s.toString());
            } catch (NumberFormatException e) {
                return defValue;
            }
        }
        return defValue;
    }

    public int getColor(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_STRING) {
            TypedValue value = valueAt(index);
            return mResources.loadColorStateList(value, value.resourceId, mTheme).getDefaultColor();
        }
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to color: type=0x"
                + Integer.toHexString(type));
    }

    public ColorStateList getColorStateList(int index) {
        checkRecycled();
        TypedValue value = peekValue(index);
        if (value == null) return null;
        if (value.type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        return mResources.loadColorStateList(value, value.resourceId, mTheme);
    }

    public int getInteger(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to integer: type=0x"
                + Integer.toHexString(type));
    }

    public float getDimension(int index, float defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimension(data(index), mMetrics);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to dimension: type=0x"
                + Integer.toHexString(type));
    }

    public int getDimensionPixelOffset(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimensionPixelOffset(data(index), mMetrics);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to dimension: type=0x"
                + Integer.toHexString(type));
    }

    public int getDimensionPixelSize(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimensionPixelSize(data(index), mMetrics);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to dimension: type=0x"
                + Integer.toHexString(type));
    }

    public int getLayoutDimension(int index, String name) {
        checkRecycled();
        final int type = type(index);
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimensionPixelSize(data(index), mMetrics);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException(getPositionDescription() + ": You must supply a " + name
                + " attribute.");
    }

    public int getLayoutDimension(int index, int defValue) {
        checkRecycled();
        final int type = type(index);
        if (type >= TypedValue.TYPE_FIRST_INT && type <= TypedValue.TYPE_LAST_INT) return data(index);
        if (type == TypedValue.TYPE_DIMENSION) return TypedValue.complexToDimensionPixelSize(data(index), mMetrics);
        return defValue;
    }

    public float getFraction(int index, int base, int pbase, float defValue) {
        checkRecycled();
        final int type = type(index);
        if (type == TypedValue.TYPE_NULL) return defValue;
        if (type == TypedValue.TYPE_FRACTION) return TypedValue.complexToFraction(data(index), base, pbase);
        if (type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        throw new UnsupportedOperationException("Can't convert value at index " + index + " to fraction: type=0x"
                + Integer.toHexString(type));
    }

    public int getResourceId(int index, int defValue) {
        checkRecycled();
        final int off = index * AssetManager.STYLE_NUM_ENTRIES;
        if (mData[off + AssetManager.STYLE_TYPE] != TypedValue.TYPE_NULL) {
            final int resid = mData[off + AssetManager.STYLE_RESOURCE_ID];
            if (resid != 0) return resid;
        }
        return defValue;
    }

    public int getThemeAttributeId(int index, int defValue) {
        checkRecycled();
        if (type(index) == TypedValue.TYPE_ATTRIBUTE) return data(index);
        return defValue;
    }

    public Drawable getDrawable(int index) {
        return getDrawableForDensity(index, 0);
    }

    public Drawable getDrawableForDensity(int index, int density) {
        checkRecycled();
        TypedValue value = peekValue(index);
        if (value == null) return null;
        if (value.type == TypedValue.TYPE_ATTRIBUTE) throw unresolvedAttribute(index);
        return mResources.loadDrawable(value, value.resourceId, density, mTheme);
    }

    public CharSequence[] getTextArray(int index) {
        checkRecycled();
        TypedValue value = peekValue(index);
        if (value == null) return null;
        return mResources.getTextArray(value.resourceId);
    }

    public boolean getValue(int index, TypedValue outValue) {
        checkRecycled();
        return getValueAt(index, outValue);
    }

    public int getType(int index) {
        checkRecycled();
        return type(index);
    }

    public boolean hasValue(int index) {
        checkRecycled();
        return type(index) != TypedValue.TYPE_NULL;
    }

    public boolean hasValueOrEmpty(int index) {
        checkRecycled();
        return type(index) != TypedValue.TYPE_NULL || data(index) == TypedValue.DATA_NULL_EMPTY;
    }

    public TypedValue peekValue(int index) {
        checkRecycled();
        TypedValue value = new TypedValue();
        return getValueAt(index, value) ? value : null;
    }

    private TypedValue valueAt(int index) {
        TypedValue v = new TypedValue();
        getValueAt(index, v);
        return v;
    }

    private boolean getValueAt(int index, TypedValue outValue) {
        final int off = index * AssetManager.STYLE_NUM_ENTRIES;
        final int type = mData[off + AssetManager.STYLE_TYPE];
        if (type == TypedValue.TYPE_NULL) return false;
        outValue.type = type;
        outValue.data = mData[off + AssetManager.STYLE_DATA];
        outValue.assetCookie = mData[off + AssetManager.STYLE_ASSET_COOKIE];
        outValue.resourceId = mData[off + AssetManager.STYLE_RESOURCE_ID];
        outValue.changingConfigurations = mData[off + AssetManager.STYLE_CHANGING_CONFIGURATIONS];
        outValue.density = mData[off + AssetManager.STYLE_DENSITY];
        outValue.sourceResourceId = mData[off + AssetManager.STYLE_SOURCE_RESOURCE_ID];
        outValue.string = type == TypedValue.TYPE_STRING ? loadStringValueAt(index) : null;
        return true;
    }

    public String getPositionDescription() {
        checkRecycled();
        return mXml != null ? mXml.getPositionDescription() : "<internal>";
    }

    public int getChangingConfigurations() {
        checkRecycled();
        return 0;
    }

    public void recycle() {
        if (mRecycled) throw new RuntimeException(toString() + " recycled twice!");
        mRecycled = true;
        mXml = null;
        mTheme = null;
    }

    @Override
    public void close() {
        recycle();
    }

    @Override
    public String toString() {
        return "TypedArray{length=" + mLength + "}";
    }
}
