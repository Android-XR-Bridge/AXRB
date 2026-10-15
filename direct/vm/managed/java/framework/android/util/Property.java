package android.util;

/** A named, typed property of a class, used by property animations. */
public abstract class Property<T, V> {
    private final String mName;
    private final Class<V> mType;

    public static <T, V> Property<T, V> of(Class<T> hostType, Class<V> valueType, String name) {
        return new ReflectiveProperty<T, V>(hostType, valueType, name);
    }

    public Property(Class<V> type, String name) {
        mName = name;
        mType = type;
    }

    public boolean isReadOnly() {
        return false;
    }

    public void set(T object, V value) {
        throw new UnsupportedOperationException("Property " + getName() + " is read-only");
    }

    public abstract V get(T object);

    public String getName() {
        return mName;
    }

    public Class<V> getType() {
        return mType;
    }
}
