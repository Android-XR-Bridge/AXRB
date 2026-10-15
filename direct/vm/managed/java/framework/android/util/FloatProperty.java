package android.util;

public abstract class FloatProperty<T> extends Property<T, Float> {
    public FloatProperty(String name) {
        super(Float.class, name);
    }

    public abstract void setValue(T object, float value);

    @Override
    public final void set(T object, Float value) {
        setValue(object, value);
    }
}
