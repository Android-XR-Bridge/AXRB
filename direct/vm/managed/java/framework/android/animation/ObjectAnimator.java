package android.animation;

import android.util.Property;

/** Animates a property of a target object, through a Property or a setter found by name. */
public final class ObjectAnimator extends ValueAnimator {
    private Object mTarget;
    private String mPropertyName;
    private Property mProperty;
    private boolean mAutoCancel = false;

    public ObjectAnimator() {}

    private ObjectAnimator(Object target, String propertyName) {
        setTarget(target);
        setPropertyName(propertyName);
    }

    private <T> ObjectAnimator(T target, Property<T, ?> property) {
        setTarget(target);
        setProperty(property);
    }

    public void setPropertyName(String propertyName) {
        if (mValues != null) {
            PropertyValuesHolder valuesHolder = mValues[0];
            String oldName = valuesHolder.getPropertyName();
            valuesHolder.setPropertyName(propertyName);
            mValuesMap.remove(oldName);
            mValuesMap.put(propertyName, valuesHolder);
        }
        mPropertyName = propertyName;
        mInitialized = false;
    }

    public void setProperty(Property property) {
        if (mValues != null) {
            PropertyValuesHolder valuesHolder = mValues[0];
            String oldName = valuesHolder.getPropertyName();
            valuesHolder.setProperty(property);
            mValuesMap.remove(oldName);
            mValuesMap.put(mPropertyName, valuesHolder);
        }
        if (mProperty != null) mPropertyName = property.getName();
        mProperty = property;
        mInitialized = false;
    }

    public String getPropertyName() {
        String propertyName = null;
        if (mPropertyName != null) {
            propertyName = mPropertyName;
        } else if (mProperty != null) {
            propertyName = mProperty.getName();
        } else if (mValues != null && mValues.length > 0) {
            for (int i = 0; i < mValues.length; ++i) {
                if (i == 0) {
                    propertyName = "";
                } else {
                    propertyName += ",";
                }
                propertyName += mValues[i].getPropertyName();
            }
        }
        return propertyName;
    }

    public static ObjectAnimator ofInt(Object target, String propertyName, int... values) {
        ObjectAnimator anim = new ObjectAnimator(target, propertyName);
        anim.setIntValues(values);
        return anim;
    }

    public static <T> ObjectAnimator ofInt(T target, Property<T, Integer> property, int... values) {
        ObjectAnimator anim = new ObjectAnimator(target, property);
        anim.setIntValues(values);
        return anim;
    }

    public static ObjectAnimator ofArgb(Object target, String propertyName, int... values) {
        ObjectAnimator animator = ofInt(target, propertyName, values);
        animator.setEvaluator(ArgbEvaluator.getInstance());
        return animator;
    }

    public static <T> ObjectAnimator ofArgb(T target, Property<T, Integer> property, int... values) {
        ObjectAnimator animator = ofInt(target, property, values);
        animator.setEvaluator(ArgbEvaluator.getInstance());
        return animator;
    }

    public static ObjectAnimator ofFloat(Object target, String propertyName, float... values) {
        ObjectAnimator anim = new ObjectAnimator(target, propertyName);
        anim.setFloatValues(values);
        return anim;
    }

    public static <T> ObjectAnimator ofFloat(T target, Property<T, Float> property, float... values) {
        ObjectAnimator anim = new ObjectAnimator(target, property);
        anim.setFloatValues(values);
        return anim;
    }

    public static ObjectAnimator ofObject(Object target, String propertyName, TypeEvaluator evaluator,
            Object... values) {
        ObjectAnimator anim = new ObjectAnimator(target, propertyName);
        anim.setObjectValues(values);
        anim.setEvaluator(evaluator);
        return anim;
    }

    public static <T, V> ObjectAnimator ofObject(T target, Property<T, V> property, TypeEvaluator<V> evaluator,
            V... values) {
        ObjectAnimator anim = new ObjectAnimator(target, property);
        anim.setObjectValues(values);
        anim.setEvaluator(evaluator);
        return anim;
    }

    public static ObjectAnimator ofPropertyValuesHolder(Object target, PropertyValuesHolder... values) {
        ObjectAnimator anim = new ObjectAnimator();
        anim.setTarget(target);
        anim.setValues(values);
        return anim;
    }

    @Override
    public void setIntValues(int... values) {
        if (mValues == null || mValues.length == 0) {
            if (mProperty != null) {
                setValues(PropertyValuesHolder.ofInt(mProperty, values));
            } else {
                setValues(PropertyValuesHolder.ofInt(mPropertyName, values));
            }
        } else {
            super.setIntValues(values);
        }
    }

    @Override
    public void setFloatValues(float... values) {
        if (mValues == null || mValues.length == 0) {
            if (mProperty != null) {
                setValues(PropertyValuesHolder.ofFloat(mProperty, values));
            } else {
                setValues(PropertyValuesHolder.ofFloat(mPropertyName, values));
            }
        } else {
            super.setFloatValues(values);
        }
    }

    @Override
    public void setObjectValues(Object... values) {
        if (mValues == null || mValues.length == 0) {
            if (mProperty != null) {
                setValues(PropertyValuesHolder.ofObject(mProperty, (TypeEvaluator) null, values));
            } else {
                setValues(PropertyValuesHolder.ofObject(mPropertyName, (TypeEvaluator) null, values));
            }
        } else {
            super.setObjectValues(values);
        }
    }

    public void setAutoCancel(boolean cancel) {
        mAutoCancel = cancel;
    }

    private boolean hasSameTargetAndProperties(Animator anim) {
        if (anim instanceof ObjectAnimator) {
            PropertyValuesHolder[] theirValues = ((ObjectAnimator) anim).getValues();
            if (((ObjectAnimator) anim).getTarget() == getTarget() && mValues.length == theirValues.length) {
                for (int i = 0; i < mValues.length; ++i) {
                    PropertyValuesHolder pvhMine = mValues[i];
                    PropertyValuesHolder pvhTheirs = theirValues[i];
                    if (pvhMine.getPropertyName() == null
                            || !pvhMine.getPropertyName().equals(pvhTheirs.getPropertyName())) {
                        return false;
                    }
                }
                return true;
            }
        }
        return false;
    }

    private static final java.util.ArrayList<ObjectAnimator> sRunning = new java.util.ArrayList<ObjectAnimator>();

    @Override
    public void start() {
        if (mAutoCancel) {
            for (ObjectAnimator other : new java.util.ArrayList<ObjectAnimator>(sRunning)) {
                if (other != this && other.mAutoCancel && hasSameTargetAndProperties(other)) other.cancel();
            }
        }
        sRunning.add(this);
        addListener(mRunningTracker);
        super.start();
    }

    private final AnimatorListenerAdapter mRunningTracker = new AnimatorListenerAdapter() {
        @Override
        public void onAnimationEnd(Animator animation) {
            sRunning.remove(ObjectAnimator.this);
            removeListener(this);
        }
    };

    @Override
    void initAnimation() {
        if (!mInitialized) {
            final Object target = getTarget();
            if (target != null && mValues != null) {
                for (PropertyValuesHolder v : mValues) {
                    if (v.mValues != null && v.mValues.length == 1) v.setupStartValue(target);
                    v.setupSetterAndGetter(target);
                }
            }
            super.initAnimation();
        }
    }

    @Override
    public ObjectAnimator setDuration(long duration) {
        super.setDuration(duration);
        return this;
    }

    public Object getTarget() {
        return mTarget;
    }

    @Override
    public void setTarget(Object target) {
        if (mTarget != target) {
            if (isStarted()) cancel();
            mTarget = target;
            mInitialized = false;
        }
    }

    @Override
    public void setupStartValues() {
        initAnimation();
    }

    @Override
    public void setupEndValues() {
        initAnimation();
    }

    @Override
    void animateValue(float fraction) {
        final Object target = getTarget();
        super.animateValue(fraction);
        if (target == null || mValues == null) return;
        for (PropertyValuesHolder v : mValues) v.setAnimatedValue(target);
    }

    @Override
    public ObjectAnimator clone() {
        return (ObjectAnimator) super.clone();
    }

    @Override
    public String toString() {
        return "ObjectAnimator@" + Integer.toHexString(hashCode()) + ", target " + getTarget();
    }
}
