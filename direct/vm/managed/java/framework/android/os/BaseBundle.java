package android.os;

import java.util.Set;

/** The methods Bundle and PersistableBundle share, as declared by Android. */
public abstract class BaseBundle {
    BaseBundle() {}

    public abstract int size();

    public abstract boolean isEmpty();

    public abstract void clear();

    public abstract boolean containsKey(String key);

    public abstract Object get(String key);

    public abstract void remove(String key);

    public abstract Set<String> keySet();

    public abstract void putAll(PersistableBundle bundle);

    public abstract void putBoolean(String key, boolean value);

    public abstract void putInt(String key, int value);

    public abstract void putLong(String key, long value);

    public abstract void putDouble(String key, double value);

    public abstract void putString(String key, String value);

    public abstract void putBooleanArray(String key, boolean[] value);

    public abstract void putIntArray(String key, int[] value);

    public abstract void putLongArray(String key, long[] value);

    public abstract void putDoubleArray(String key, double[] value);

    public abstract void putStringArray(String key, String[] value);

    public abstract boolean getBoolean(String key);

    public abstract boolean getBoolean(String key, boolean defaultValue);

    public abstract int getInt(String key);

    public abstract int getInt(String key, int defaultValue);

    public abstract long getLong(String key);

    public abstract long getLong(String key, long defaultValue);

    public abstract double getDouble(String key);

    public abstract double getDouble(String key, double defaultValue);

    public abstract String getString(String key);

    public abstract String getString(String key, String defaultValue);

    public abstract boolean[] getBooleanArray(String key);

    public abstract int[] getIntArray(String key);

    public abstract long[] getLongArray(String key);

    public abstract double[] getDoubleArray(String key);

    public abstract String[] getStringArray(String key);
}
