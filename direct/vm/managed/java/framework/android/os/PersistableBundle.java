package android.os;

import java.util.HashMap;
import java.util.Set;

/** A bundle restricted to persistable value types. */
public final class PersistableBundle extends BaseBundle implements Cloneable, Parcelable {
    public static final PersistableBundle EMPTY = new PersistableBundle();

    public static final Parcelable.Creator<PersistableBundle> CREATOR = new Parcelable.Creator<PersistableBundle>() {
        public PersistableBundle createFromParcel(Parcel in) {
            PersistableBundle b = new PersistableBundle();
            Bundle inner = in.readBundle();
            if (inner != null) for (String k : inner.keySet()) b.map.put(k, inner.get(k));
            return b;
        }

        public PersistableBundle[] newArray(int size) {
            return new PersistableBundle[size];
        }
    };

    private final HashMap<String, Object> map = new HashMap<String, Object>();

    public PersistableBundle() {}

    public PersistableBundle(int capacity) {}

    public PersistableBundle(PersistableBundle b) {
        map.putAll(b.map);
    }

    @Override
    public Object clone() {
        return new PersistableBundle(this);
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

    public void putAll(PersistableBundle bundle) {
        map.putAll(bundle.map);
    }

    public void putBoolean(String key, boolean value) {
        map.put(key, value);
    }

    public void putInt(String key, int value) {
        map.put(key, value);
    }

    public void putLong(String key, long value) {
        map.put(key, value);
    }

    public void putDouble(String key, double value) {
        map.put(key, value);
    }

    public void putString(String key, String value) {
        map.put(key, value);
    }

    public void putBooleanArray(String key, boolean[] value) {
        map.put(key, value);
    }

    public void putIntArray(String key, int[] value) {
        map.put(key, value);
    }

    public void putLongArray(String key, long[] value) {
        map.put(key, value);
    }

    public void putDoubleArray(String key, double[] value) {
        map.put(key, value);
    }

    public void putStringArray(String key, String[] value) {
        map.put(key, value);
    }

    public void putPersistableBundle(String key, PersistableBundle value) {
        map.put(key, value);
    }

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object o = map.get(key);
        return o instanceof Boolean ? (Boolean) o : defaultValue;
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object o = map.get(key);
        return o instanceof Integer ? (Integer) o : defaultValue;
    }

    public long getLong(String key) {
        return getLong(key, 0L);
    }

    public long getLong(String key, long defaultValue) {
        Object o = map.get(key);
        return o instanceof Long ? (Long) o : defaultValue;
    }

    public double getDouble(String key) {
        return getDouble(key, 0.0);
    }

    public double getDouble(String key, double defaultValue) {
        Object o = map.get(key);
        return o instanceof Double ? (Double) o : defaultValue;
    }

    public String getString(String key) {
        Object o = map.get(key);
        return o instanceof String ? (String) o : null;
    }

    public String getString(String key, String defaultValue) {
        String s = getString(key);
        return s == null ? defaultValue : s;
    }

    public boolean[] getBooleanArray(String key) {
        Object o = map.get(key);
        return o instanceof boolean[] ? (boolean[]) o : null;
    }

    public int[] getIntArray(String key) {
        Object o = map.get(key);
        return o instanceof int[] ? (int[]) o : null;
    }

    public long[] getLongArray(String key) {
        Object o = map.get(key);
        return o instanceof long[] ? (long[]) o : null;
    }

    public double[] getDoubleArray(String key) {
        Object o = map.get(key);
        return o instanceof double[] ? (double[]) o : null;
    }

    public String[] getStringArray(String key) {
        Object o = map.get(key);
        return o instanceof String[] ? (String[]) o : null;
    }

    public PersistableBundle getPersistableBundle(String key) {
        Object o = map.get(key);
        return o instanceof PersistableBundle ? (PersistableBundle) o : null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        Bundle b = new Bundle(this);
        dest.writeBundle(b);
    }

    @Override
    public String toString() {
        return "PersistableBundle[" + map + "]";
    }
}
