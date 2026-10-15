package java.lang;

import java.io.Serializable;

/**
 * A class mirror. `vmClass` points at the runtime's own class structure and is
 * set only by the runtime; instances cannot be created from Java.
 */
public final class Class<T> implements Serializable {
    private transient long vmClass;
    private transient String name;

    private Class() {}

    public String getName() {
        String n = name;
        if (n == null) {
            n = nativeGetName();
            name = n;
        }
        return n;
    }

    private native String nativeGetName();

    /**
     * Derived from the binary name (javac's Outer$Inner and Outer$1 conventions),
     * not from InnerClass annotations; exact for javac/d8 output.
     */
    public String getSimpleName() {
        if (isArray()) return getComponentType().getSimpleName() + "[]";
        String n = getName();
        int dot = n.lastIndexOf('.');
        String simple = n.substring(dot + 1);
        int dollar = simple.lastIndexOf('$');
        if (dollar >= 0) {
            simple = simple.substring(dollar + 1);
            // Anonymous classes are named Outer$1; their simple name is empty.
            boolean digits = !simple.isEmpty();
            for (int i = 0; i < simple.length(); i++) {
                if (!Character.isDigit(simple.charAt(i))) digits = false;
            }
            if (digits) return "";
        }
        return simple;
    }

    public String getCanonicalName() {
        if (isArray()) {
            String c = getComponentType().getCanonicalName();
            return c == null ? null : c + "[]";
        }
        return getName().replace('$', '.');
    }

    public String getPackageName() {
        Class<?> c = this;
        while (c.isArray()) c = c.getComponentType();
        if (c.isPrimitive()) return "java.lang";
        String n = c.getName();
        int dot = n.lastIndexOf('.');
        return dot < 0 ? "" : n.substring(0, dot);
    }

    public Package getPackage() {
        String name = getPackageName();
        return name.isEmpty() ? null : new Package(name);
    }

    public boolean isMemberClass() {
        return !isArray() && !isAnonymousClass() && getName().indexOf('$') > 0;
    }

    /** Java resources packaged in the APK are not exposed. */
    public java.io.InputStream getResourceAsStream(String name) {
        return null;
    }

    /** Runtime annotations are not retained by this runtime: none are present. */
    public <A extends java.lang.annotation.Annotation> A getAnnotation(Class<A> annotationClass) {
        if (annotationClass == null) throw new NullPointerException();
        return null;
    }

    public boolean isAnnotationPresent(Class<? extends java.lang.annotation.Annotation> annotationClass) {
        return getAnnotation(annotationClass) != null;
    }

    public java.lang.annotation.Annotation[] getAnnotations() {
        return new java.lang.annotation.Annotation[0];
    }

    public String getTypeName() {
        if (isArray()) return getComponentType().getTypeName() + "[]";
        return getName();
    }

    @Override
    public String toString() {
        return (isInterface() ? "interface " : isPrimitive() ? "" : "class ") + getName();
    }

    public native Class<? super T> getSuperclass();

    public native Class<?>[] getInterfaces();

    public native Class<?> getComponentType();

    public native boolean isArray();

    public native boolean isInterface();

    public native boolean isPrimitive();

    public native int getModifiers();

    public boolean isEnum() {
        return (getModifiers() & 0x4000) != 0 && getSuperclass() == Enum.class;
    }

    public boolean isAnnotation() {
        return (getModifiers() & 0x2000) != 0;
    }

    public boolean isSynthetic() {
        return (getModifiers() & 0x1000) != 0;
    }

    public boolean isAnonymousClass() {
        return getSimpleName().isEmpty() && !isArray();
    }

    public native boolean isInstance(Object obj);

    public native boolean isAssignableFrom(Class<?> cls);

    public native ClassLoader getClassLoader();

    @SuppressWarnings("unchecked")
    public T cast(Object obj) {
        if (obj != null && !isInstance(obj)) {
            throw new ClassCastException("Cannot cast " + obj.getClass().getName() + " to " + getName());
        }
        return (T) obj;
    }

    @SuppressWarnings("unchecked")
    public <U> Class<? extends U> asSubclass(Class<U> clazz) {
        if (clazz.isAssignableFrom(this)) return (Class<? extends U>) this;
        throw new ClassCastException(toString());
    }

    /** Runs the public or accessible no-argument constructor. */
    public native T newInstance() throws InstantiationException, IllegalAccessException;

    public static native Class<?> forName(String className) throws ClassNotFoundException;

    public static native Class<?> forName(String name, boolean initialize, ClassLoader loader)
            throws ClassNotFoundException;

    static native Class<?> getPrimitiveClass(String name);

    /** Enum constants in declaration order, or null if this is not an enum. */
    native T[] getEnumConstantsShared();

    public T[] getEnumConstants() {
        T[] values = getEnumConstantsShared();
        return values == null ? null : values.clone();
    }

    public boolean desiredAssertionStatus() {
        return false;
    }

    public java.lang.reflect.Method getMethod(String name, Class<?>... parameterTypes) throws NoSuchMethodException {
        java.lang.reflect.Method m = nativeGetMethod(name, parameterTypes, true);
        if (m == null) throw new NoSuchMethodException(getName() + "." + name);
        return m;
    }

    public java.lang.reflect.Method getDeclaredMethod(String name, Class<?>... parameterTypes)
            throws NoSuchMethodException {
        java.lang.reflect.Method m = nativeGetMethod(name, parameterTypes, false);
        if (m == null) throw new NoSuchMethodException(getName() + "." + name);
        return m;
    }

    public java.lang.reflect.Constructor<T> getConstructor(Class<?>... parameterTypes) throws NoSuchMethodException {
        return getDeclaredConstructor(parameterTypes);
    }

    @SuppressWarnings("unchecked")
    public java.lang.reflect.Constructor<?>[] getConstructors() {
        return getDeclaredConstructors0(true);
    }

    public java.lang.reflect.Constructor<?>[] getDeclaredConstructors() {
        return getDeclaredConstructors0(false);
    }

    private native java.lang.reflect.Constructor<?>[] getDeclaredConstructors0(boolean publicOnly);

    /** Public methods of this class, its superclasses and its interfaces (one per signature). */
    public java.lang.reflect.Method[] getMethods() {
        java.util.ArrayList<java.lang.reflect.Method> out = new java.util.ArrayList<java.lang.reflect.Method>();
        java.util.HashSet<String> seen = new java.util.HashSet<String>();
        java.util.ArrayList<Class<?>> order = new java.util.ArrayList<Class<?>>();
        for (Class<?> k = this; k != null; k = k.getSuperclass()) order.add(k);
        for (int i = 0; i < order.size(); i++) {
            for (Class<?> iface : order.get(i).getInterfaces()) {
                if (!order.contains(iface)) order.add(iface);
            }
        }
        for (Class<?> k : order) {
            for (java.lang.reflect.Method m : k.getDeclaredMethods()) {
                if (!java.lang.reflect.Modifier.isPublic(m.getModifiers())) continue;
                StringBuilder key = new StringBuilder(m.getName()).append('(');
                for (Class<?> p : m.getParameterTypes()) key.append(p.getName()).append(',');
                if (seen.add(key.toString())) out.add(m);
            }
        }
        return out.toArray(new java.lang.reflect.Method[out.size()]);
    }

    public java.lang.reflect.Field[] getFields() {
        java.util.ArrayList<java.lang.reflect.Field> out = new java.util.ArrayList<java.lang.reflect.Field>();
        for (Class<?> k = this; k != null; k = k.getSuperclass()) {
            for (java.lang.reflect.Field f : k.getDeclaredFields()) {
                if (java.lang.reflect.Modifier.isPublic(f.getModifiers())) out.add(f);
            }
            for (Class<?> iface : k.getInterfaces()) {
                for (java.lang.reflect.Field f : iface.getDeclaredFields()) out.add(f);
            }
        }
        return out.toArray(new java.lang.reflect.Field[out.size()]);
    }

    public java.lang.reflect.Constructor<T> getDeclaredConstructor(Class<?>... parameterTypes)
            throws NoSuchMethodException {
        java.lang.reflect.Constructor<T> c = nativeGetConstructor(parameterTypes);
        if (c == null) throw new NoSuchMethodException(getName() + ".<init>");
        return c;
    }

    public java.lang.reflect.Field getField(String name) throws NoSuchFieldException {
        java.lang.reflect.Field f = nativeGetField(name, true);
        if (f == null) throw new NoSuchFieldException(name);
        return f;
    }

    public java.lang.reflect.Field getDeclaredField(String name) throws NoSuchFieldException {
        java.lang.reflect.Field f = nativeGetField(name, false);
        if (f == null) throw new NoSuchFieldException(name);
        return f;
    }

    public native java.lang.reflect.Method[] getDeclaredMethods();

    public native java.lang.reflect.Field[] getDeclaredFields();

    private native java.lang.reflect.Method nativeGetMethod(String name, Class<?>[] params, boolean publicOnly);

    private native java.lang.reflect.Constructor<T> nativeGetConstructor(Class<?>[] params);

    private native java.lang.reflect.Field nativeGetField(String name, boolean publicOnly);
}
