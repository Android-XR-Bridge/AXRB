package android.os;

import android.util.Size;
import android.util.SizeF;
import android.util.SparseArray;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * A string-keyed map of values that can be flattened into a {@link Parcel}.
 * Typed getters return the default when the key is missing or holds another
 * type, as on Android.
 */
public final class Bundle extends BaseBundle implements Cloneable, Parcelable {
    public static final Bundle EMPTY = new Bundle();

    public static final Parcelable.Creator<Bundle> CREATOR = new Parcelable.Creator<Bundle>() {
        public Bundle createFromParcel(Parcel in) {
            return in.readBundle();
        }

        public Bundle[] newArray(int size) {
            return new Bundle[size];
        }
    };

    private final HashMap<String, Object> map;
    private ClassLoader classLoader;

    public Bundle() {
        map = new HashMap<String, Object>();
    }

    public Bundle(int capacity) {
        map = new HashMap<String, Object>();
    }

    public Bundle(Bundle b) {
        map = new HashMap<String, Object>(b.map);
        classLoader = b.classLoader;
    }

    public Bundle(PersistableBundle b) {
        this();
        for (String key : b.keySet()) map.put(key, b.get(key));
    }

    public Bundle(ClassLoader loader) {
        this();
        classLoader = loader;
    }

    public static Bundle forPair(String key, String value) {
        Bundle b = new Bundle(1);
        b.putString(key, value);
        return b;
    }

    @Override
    public Object clone() {
        return new Bundle(this);
    }

    public Bundle deepCopy() {
        Parcel p = Parcel.obtain();
        writeToParcel(p, 0);
        p.setDataPosition(0);
        Bundle b = new Bundle(classLoader);
        b.readFromParcel(p);
        p.recycle();
        return b;
    }

    public void setClassLoader(ClassLoader loader) {
        classLoader = loader;
    }

    public ClassLoader getClassLoader() {
        return classLoader;
    }

    public int size() {
        return map.size();
    }

    public boolean isEmpty() {
        return map.isEmpty();
    }

    public void clear() {
        map.clear();
    }

    public boolean containsKey(String key) {
        return map.containsKey(key);
    }

    public Object get(String key) {
        return map.get(key);
    }

    public void remove(String key) {
        map.remove(key);
    }

    public Set<String> keySet() {
        return map.keySet();
    }

    public void putAll(Bundle bundle) {
        map.putAll(bundle.map);
    }

    public void putAll(PersistableBundle bundle) {
        for (String key : bundle.keySet()) map.put(key, bundle.get(key));
    }

    public boolean hasFileDescriptors() {
        return false;
    }

    // --- put ---------------------------------------------------------------------

    public void putBoolean(String key, boolean value) {
        map.put(key, value);
    }

    public void putByte(String key, byte value) {
        map.put(key, value);
    }

    public void putChar(String key, char value) {
        map.put(key, value);
    }

    public void putShort(String key, short value) {
        map.put(key, value);
    }

    public void putInt(String key, int value) {
        map.put(key, value);
    }

    public void putLong(String key, long value) {
        map.put(key, value);
    }

    public void putFloat(String key, float value) {
        map.put(key, value);
    }

    public void putDouble(String key, double value) {
        map.put(key, value);
    }

    public void putString(String key, String value) {
        map.put(key, value);
    }

    public void putCharSequence(String key, CharSequence value) {
        map.put(key, value);
    }

    public void putBundle(String key, Bundle value) {
        map.put(key, value);
    }

    public void putParcelable(String key, Parcelable value) {
        map.put(key, value);
    }

    public void putParcelableArray(String key, Parcelable[] value) {
        map.put(key, value);
    }

    public void putParcelableArrayList(String key, ArrayList<? extends Parcelable> value) {
        map.put(key, value);
    }

    public void putSparseParcelableArray(String key, SparseArray<? extends Parcelable> value) {
        map.put(key, value);
    }

    public void putSerializable(String key, Serializable value) {
        map.put(key, value);
    }

    public void putSize(String key, Size value) {
        map.put(key, value);
    }

    public void putSizeF(String key, SizeF value) {
        map.put(key, value);
    }

    public void putBinder(String key, IBinder value) {
        map.put(key, value);
    }

    public void putBooleanArray(String key, boolean[] value) {
        map.put(key, value);
    }

    public void putByteArray(String key, byte[] value) {
        map.put(key, value);
    }

    public void putShortArray(String key, short[] value) {
        map.put(key, value);
    }

    public void putCharArray(String key, char[] value) {
        map.put(key, value);
    }

    public void putIntArray(String key, int[] value) {
        map.put(key, value);
    }

    public void putLongArray(String key, long[] value) {
        map.put(key, value);
    }

    public void putFloatArray(String key, float[] value) {
        map.put(key, value);
    }

    public void putDoubleArray(String key, double[] value) {
        map.put(key, value);
    }

    public void putStringArray(String key, String[] value) {
        map.put(key, value);
    }

    public void putCharSequenceArray(String key, CharSequence[] value) {
        map.put(key, value);
    }

    public void putIntegerArrayList(String key, ArrayList<Integer> value) {
        map.put(key, value);
    }

    public void putStringArrayList(String key, ArrayList<String> value) {
        map.put(key, value);
    }

    public void putCharSequenceArrayList(String key, ArrayList<CharSequence> value) {
        map.put(key, value);
    }

    // --- get ---------------------------------------------------------------------

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object o = map.get(key);
        return o instanceof Boolean ? ((Boolean) o).booleanValue() : defaultValue;
    }

    public byte getByte(String key) {
        return getByte(key, (byte) 0);
    }

    public Byte getByte(String key, byte defaultValue) {
        Object o = map.get(key);
        return o instanceof Byte ? (Byte) o : Byte.valueOf(defaultValue);
    }

    public char getChar(String key) {
        return getChar(key, (char) 0);
    }

    public char getChar(String key, char defaultValue) {
        Object o = map.get(key);
        return o instanceof Character ? ((Character) o).charValue() : defaultValue;
    }

    public short getShort(String key) {
        return getShort(key, (short) 0);
    }

    public short getShort(String key, short defaultValue) {
        Object o = map.get(key);
        return o instanceof Short ? ((Short) o).shortValue() : defaultValue;
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object o = map.get(key);
        return o instanceof Integer ? ((Integer) o).intValue() : defaultValue;
    }

    public long getLong(String key) {
        return getLong(key, 0L);
    }

    public long getLong(String key, long defaultValue) {
        Object o = map.get(key);
        return o instanceof Long ? ((Long) o).longValue() : defaultValue;
    }

    public float getFloat(String key) {
        return getFloat(key, 0f);
    }

    public float getFloat(String key, float defaultValue) {
        Object o = map.get(key);
        return o instanceof Float ? ((Float) o).floatValue() : defaultValue;
    }

    public double getDouble(String key) {
        return getDouble(key, 0.0);
    }

    public double getDouble(String key, double defaultValue) {
        Object o = map.get(key);
        return o instanceof Double ? ((Double) o).doubleValue() : defaultValue;
    }

    public String getString(String key) {
        Object o = map.get(key);
        return o instanceof String ? (String) o : null;
    }

    public String getString(String key, String defaultValue) {
        String s = getString(key);
        return s == null ? defaultValue : s;
    }

    public CharSequence getCharSequence(String key) {
        Object o = map.get(key);
        return o instanceof CharSequence ? (CharSequence) o : null;
    }

    public CharSequence getCharSequence(String key, CharSequence defaultValue) {
        CharSequence s = getCharSequence(key);
        return s == null ? defaultValue : s;
    }

    public Bundle getBundle(String key) {
        Object o = map.get(key);
        return o instanceof Bundle ? (Bundle) o : null;
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> T getParcelable(String key) {
        Object o = map.get(key);
        return o instanceof Parcelable ? (T) o : null;
    }

    public <T> T getParcelable(String key, Class<T> clazz) {
        Object o = map.get(key);
        return clazz.isInstance(o) ? clazz.cast(o) : null;
    }

    public Parcelable[] getParcelableArray(String key) {
        Object o = map.get(key);
        return o instanceof Parcelable[] ? (Parcelable[]) o : null;
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> ArrayList<T> getParcelableArrayList(String key) {
        Object o = map.get(key);
        return o instanceof ArrayList ? (ArrayList<T>) o : null;
    }

    @SuppressWarnings("unchecked")
    public <T extends Parcelable> SparseArray<T> getSparseParcelableArray(String key) {
        Object o = map.get(key);
        return o instanceof SparseArray ? (SparseArray<T>) o : null;
    }

    public Serializable getSerializable(String key) {
        Object o = map.get(key);
        return o instanceof Serializable ? (Serializable) o : null;
    }

    public <T extends Serializable> T getSerializable(String key, Class<T> clazz) {
        Object o = map.get(key);
        return clazz.isInstance(o) ? clazz.cast(o) : null;
    }

    public Size getSize(String key) {
        Object o = map.get(key);
        return o instanceof Size ? (Size) o : null;
    }

    public SizeF getSizeF(String key) {
        Object o = map.get(key);
        return o instanceof SizeF ? (SizeF) o : null;
    }

    public IBinder getBinder(String key) {
        Object o = map.get(key);
        return o instanceof IBinder ? (IBinder) o : null;
    }

    public boolean[] getBooleanArray(String key) {
        Object o = map.get(key);
        return o instanceof boolean[] ? (boolean[]) o : null;
    }

    public byte[] getByteArray(String key) {
        Object o = map.get(key);
        return o instanceof byte[] ? (byte[]) o : null;
    }

    public short[] getShortArray(String key) {
        Object o = map.get(key);
        return o instanceof short[] ? (short[]) o : null;
    }

    public char[] getCharArray(String key) {
        Object o = map.get(key);
        return o instanceof char[] ? (char[]) o : null;
    }

    public int[] getIntArray(String key) {
        Object o = map.get(key);
        return o instanceof int[] ? (int[]) o : null;
    }

    public long[] getLongArray(String key) {
        Object o = map.get(key);
        return o instanceof long[] ? (long[]) o : null;
    }

    public float[] getFloatArray(String key) {
        Object o = map.get(key);
        return o instanceof float[] ? (float[]) o : null;
    }

    public double[] getDoubleArray(String key) {
        Object o = map.get(key);
        return o instanceof double[] ? (double[]) o : null;
    }

    public String[] getStringArray(String key) {
        Object o = map.get(key);
        return o instanceof String[] ? (String[]) o : null;
    }

    public CharSequence[] getCharSequenceArray(String key) {
        Object o = map.get(key);
        return o instanceof CharSequence[] ? (CharSequence[]) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<Integer> getIntegerArrayList(String key) {
        Object o = map.get(key);
        return o instanceof ArrayList ? (ArrayList<Integer>) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<String> getStringArrayList(String key) {
        Object o = map.get(key);
        return o instanceof ArrayList ? (ArrayList<String>) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<CharSequence> getCharSequenceArrayList(String key) {
        Object o = map.get(key);
        return o instanceof ArrayList ? (ArrayList<CharSequence>) o : null;
    }

    // --- parceling ---------------------------------------------------------------

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
        parcel.writeInt(map.size());
        for (Map.Entry<String, Object> e : map.entrySet()) {
            parcel.writeString(e.getKey());
            Object v = e.getValue();
            if (v instanceof short[]) {
                // Parcel.writeValue has no short[]; keep the type.
                short[] a = (short[]) v;
                int[] wide = new int[a.length];
                for (int i = 0; i < a.length; i++) wide[i] = a[i];
                parcel.writeInt(1);
                parcel.writeIntArray(wide);
            } else if (v instanceof Size || v instanceof SizeF) {
                parcel.writeInt(2);
                parcel.writeValue(new ParcelledValue(v));
            } else {
                parcel.writeInt(0);
                parcel.writeValue(v);
            }
        }
    }

    public void readFromParcel(Parcel parcel) {
        map.clear();
        int n = parcel.readInt();
        for (int i = 0; i < n; i++) {
            String key = parcel.readString();
            int kind = parcel.readInt();
            if (kind == 1) {
                int[] wide = parcel.createIntArray();
                short[] a = new short[wide.length];
                for (int j = 0; j < a.length; j++) a[j] = (short) wide[j];
                map.put(key, a);
            } else if (kind == 2) {
                Object v = parcel.readValue(classLoader);
                map.put(key, v instanceof ParcelledValue ? ((ParcelledValue) v).value : v);
            } else {
                map.put(key, parcel.readValue(classLoader));
            }
        }
    }

    /** Immutable values without a parcel form of their own are shared. */
    private static final class ParcelledValue implements Serializable {
        final Object value;

        ParcelledValue(Object value) {
            this.value = value;
        }
    }

    @Override
    public String toString() {
        return "Bundle[" + map.toString() + "]";
    }
}
