package android.util;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/** A property backed by getter/setter methods or a public field, found by reflection. */
class ReflectiveProperty<T, V> extends Property<T, V> {
    private static final String PREFIX_GET = "get";
    private static final String PREFIX_IS = "is";
    private static final String PREFIX_SET = "set";
    private Method mSetter;
    private Method mGetter;
    private Field mField;

    public ReflectiveProperty(Class<T> propertyHolder, Class<V> valueType, String name) {
        super(valueType, name);
        char firstLetter = Character.toUpperCase(name.charAt(0));
        String theRest = name.substring(1);
        String capitalizedName = firstLetter + theRest;
        String getterName = PREFIX_GET + capitalizedName;
        try {
            mGetter = propertyHolder.getMethod(getterName, (Class<?>[]) null);
        } catch (NoSuchMethodException e) {
            getterName = PREFIX_IS + capitalizedName;
            try {
                mGetter = propertyHolder.getMethod(getterName, (Class<?>[]) null);
            } catch (NoSuchMethodException e1) {
                try {
                    mField = propertyHolder.getField(name);
                    return;
                } catch (NoSuchFieldException e2) {
                    throw new NoSuchPropertyException("No accessor method or field found for property with name "
                            + name);
                }
            }
        }
        String setterName = PREFIX_SET + capitalizedName;
        try {
            mSetter = propertyHolder.getMethod(setterName, mGetter.getReturnType());
        } catch (NoSuchMethodException ignored) {
            // Read-only.
        }
    }

    @Override
    public void set(T object, V value) {
        if (mSetter != null) {
            try {
                mSetter.invoke(object, value);
            } catch (IllegalAccessException e) {
                throw new AssertionError();
            } catch (java.lang.reflect.InvocationTargetException e) {
                throw new RuntimeException(e.getCause());
            }
        } else if (mField != null) {
            try {
                mField.set(object, value);
            } catch (IllegalAccessException e) {
                throw new AssertionError();
            }
        } else {
            throw new UnsupportedOperationException("Property " + getName() + " is read-only");
        }
    }

    @Override
    @SuppressWarnings("unchecked")
    public V get(T object) {
        if (mGetter != null) {
            try {
                return (V) mGetter.invoke(object, (Object[]) null);
            } catch (IllegalAccessException e) {
                throw new AssertionError();
            } catch (java.lang.reflect.InvocationTargetException e) {
                throw new RuntimeException(e.getCause());
            }
        } else if (mField != null) {
            try {
                return (V) mField.get(object);
            } catch (IllegalAccessException e) {
                throw new AssertionError();
            }
        }
        throw new AssertionError();
    }

    @Override
    public boolean isReadOnly() {
        return mSetter == null && mField == null;
    }
}
