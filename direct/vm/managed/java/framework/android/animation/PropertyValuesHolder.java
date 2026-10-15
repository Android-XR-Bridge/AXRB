package android.animation;

import android.util.Property;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** A property and the values it takes during an animation (evenly spaced keyframes). */
public class PropertyValuesHolder implements Cloneable {
    String mPropertyName;
    protected Property mProperty;
    Method mSetter = null;
    private Method mGetter = null;
    Class mValueType;
    Object[] mValues;
    private TypeEvaluator mEvaluator;
    private Object mAnimatedValue;

    private static final TypeEvaluator sIntEvaluator = new IntEvaluator();
    private static final TypeEvaluator sFloatEvaluator = new FloatEvaluator();

    private PropertyValuesHolder(String propertyName) {
        mPropertyName = propertyName;
    }

    private PropertyValuesHolder(Property property) {
        mProperty = property;
        if (property != null) mPropertyName = property.getName();
    }

    public static PropertyValuesHolder ofInt(String propertyName, int... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(propertyName);
        p.setIntValues(values);
        return p;
    }

    public static PropertyValuesHolder ofInt(Property<?, Integer> property, int... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(property);
        p.setIntValues(values);
        return p;
    }

    public static PropertyValuesHolder ofFloat(String propertyName, float... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(propertyName);
        p.setFloatValues(values);
        return p;
    }

    public static PropertyValuesHolder ofFloat(Property<?, Float> property, float... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(property);
        p.setFloatValues(values);
        return p;
    }

    public static PropertyValuesHolder ofObject(String propertyName, TypeEvaluator evaluator, Object... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(propertyName);
        p.setObjectValues(values);
        p.setEvaluator(evaluator);
        return p;
    }

    public static <V> PropertyValuesHolder ofObject(Property property, TypeEvaluator<V> evaluator, V... values) {
        PropertyValuesHolder p = new PropertyValuesHolder(property);
        p.setObjectValues(values);
        p.setEvaluator(evaluator);
        return p;
    }

    public void setIntValues(int... values) {
        mValueType = int.class;
        mValues = new Object[values.length];
        for (int i = 0; i < values.length; i++) mValues[i] = values[i];
    }

    public void setFloatValues(float... values) {
        mValueType = float.class;
        mValues = new Object[values.length];
        for (int i = 0; i < values.length; i++) mValues[i] = values[i];
    }

    public void setObjectValues(Object... values) {
        mValueType = values.length > 0 && values[0] != null ? values[0].getClass() : Object.class;
        mValues = values.clone();
    }

    public void setEvaluator(TypeEvaluator evaluator) {
        mEvaluator = evaluator;
    }

    public void setPropertyName(String propertyName) {
        mPropertyName = propertyName;
    }

    public void setProperty(Property property) {
        mProperty = property;
    }

    public String getPropertyName() {
        return mPropertyName;
    }

    @Override
    public PropertyValuesHolder clone() {
        try {
            PropertyValuesHolder newPVH = (PropertyValuesHolder) super.clone();
            newPVH.mValues = mValues.clone();
            return newPVH;
        } catch (CloneNotSupportedException e) {
            return null;
        }
    }

    void init() {
        if (mEvaluator == null) {
            if (mValueType == int.class || mValueType == Integer.class) {
                mEvaluator = sIntEvaluator;
            } else if (mValueType == float.class || mValueType == Float.class) {
                mEvaluator = sFloatEvaluator;
            }
        }
    }

    private static String getMethodName(String prefix, String propertyName) {
        if (propertyName == null || propertyName.length() == 0) return prefix;
        char firstLetter = Character.toUpperCase(propertyName.charAt(0));
        return prefix + firstLetter + propertyName.substring(1);
    }

    private Method findMethod(Class targetClass, String prefix, Class valueType) {
        String name = getMethodName(prefix, mPropertyName);
        if (valueType == null) {
            try {
                return targetClass.getMethod(name);
            } catch (NoSuchMethodException e) {
                return null;
            }
        }
        Class[] candidates;
        if (valueType == float.class || valueType == Float.class) {
            candidates = new Class[] {float.class, Float.class, double.class, int.class, Double.class, Integer.class};
        } else if (valueType == int.class || valueType == Integer.class) {
            candidates = new Class[] {int.class, Integer.class, float.class, double.class, Float.class, Double.class};
        } else {
            candidates = new Class[] {valueType};
        }
        for (Class c : candidates) {
            try {
                Method m = targetClass.getMethod(name, c);
                mValueType = c;
                return m;
            } catch (NoSuchMethodException e) {
                // Try the next parameter type.
            }
        }
        return null;
    }

    void setupSetterAndGetter(Object target) {
        if (mProperty != null) {
            try {
                for (int i = 0; i < mValues.length; i++) {
                    if (mValues[i] == null) mValues[i] = mProperty.get(target);
                }
                return;
            } catch (ClassCastException e) {
                mProperty = null;
            }
        }
        Class targetClass = target.getClass();
        if (mSetter == null) {
            mSetter = findMethod(targetClass, "set", mValueType);
            if (mSetter == null) {
                android.util.Log.w("PropertyValuesHolder", "Method " + getMethodName("set", mPropertyName)
                        + "() with type " + mValueType + " not found on target class " + targetClass);
            }
        }
        for (int i = 0; i < mValues.length; i++) {
            if (mValues[i] == null) {
                if (mGetter == null) mGetter = findMethod(targetClass, "get", null);
                if (mGetter != null) {
                    try {
                        mValues[i] = mGetter.invoke(target);
                    } catch (InvocationTargetException | IllegalAccessException e) {
                        android.util.Log.e("PropertyValuesHolder", e.toString());
                    }
                }
            }
        }
    }

    /** One value given: it is the end value, and the start comes from the target (ObjectAnimator). */
    void setupStartValue(Object target) {
        Object start = null;
        if (mProperty != null) {
            start = mProperty.get(target);
        } else {
            if (mGetter == null) mGetter = findMethod(target.getClass(), "get", null);
            if (mGetter != null) {
                try {
                    start = mGetter.invoke(target);
                } catch (InvocationTargetException | IllegalAccessException e) {
                    android.util.Log.e("PropertyValuesHolder", e.toString());
                }
            }
        }
        if (start != null && mValues.length == 1) mValues = new Object[] {start, mValues[0]};
    }

    void setAnimatedValue(Object target) {
        if (mProperty != null) {
            mProperty.set(target, getAnimatedValue());
            return;
        }
        if (mSetter != null) {
            try {
                Object v = getAnimatedValue();
                if (mValueType == int.class || mValueType == Integer.class) {
                    v = ((Number) v).intValue();
                } else if (mValueType == float.class || mValueType == Float.class) {
                    v = ((Number) v).floatValue();
                } else if (mValueType == double.class || mValueType == Double.class) {
                    v = ((Number) v).doubleValue();
                }
                mSetter.invoke(target, v);
            } catch (InvocationTargetException | IllegalAccessException e) {
                android.util.Log.e("PropertyValuesHolder", e.toString());
            }
        }
    }

    @SuppressWarnings("unchecked")
    void calculateValue(float fraction) {
        final int n = mValues.length;
        if (n == 1) {
            mAnimatedValue = mValues[0];
            return;
        }
        if (fraction <= 0f) {
            mAnimatedValue = mEvaluator.evaluate(fraction * (n - 1), mValues[0], mValues[1]);
            return;
        }
        if (fraction >= 1f) {
            float f = (fraction - (n - 2) / (float) (n - 1)) * (n - 1);
            mAnimatedValue = mEvaluator.evaluate(f, mValues[n - 2], mValues[n - 1]);
            return;
        }
        float scaled = fraction * (n - 1);
        int index = Math.min((int) scaled, n - 2);
        mAnimatedValue = mEvaluator.evaluate(scaled - index, mValues[index], mValues[index + 1]);
    }

    Object getAnimatedValue() {
        return mAnimatedValue;
    }

    @Override
    public String toString() {
        return mPropertyName + ": " + java.util.Arrays.toString(mValues);
    }
}
