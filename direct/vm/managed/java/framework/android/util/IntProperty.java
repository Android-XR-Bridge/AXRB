package android.util;

public abstract class IntProperty<T> extends Property<T, Integer> {
    public IntProperty(String name) {
        super(Integer.class, name);
    }

    public abstract void setValue(T object, int value);

    @Override
    public final void set(T object, Integer value) {
        setValue(object, value.intValue());
    }
}
