package com.axrb.runtime;

import java.lang.reflect.*;

final class Reflect {
    static Object field(Object target, String name) throws Exception {
        Field field = findField(target instanceof Class ? (Class<?>) target : target.getClass(), name);
        return field.get(target instanceof Class ? null : target);
    }
    static void set(Object target, String name, Object value) throws Exception {
        Field field = findField(target instanceof Class ? (Class<?>) target : target.getClass(), name);
        field.set(target instanceof Class ? null : target, value);
    }
    static Field findField(Class<?> type, String name) throws Exception {
        for (Class<?> c = type; c != null; c = c.getSuperclass()) {
            try { Field f = c.getDeclaredField(name); f.setAccessible(true); return f; }
            catch (NoSuchFieldException ignored) {}
        }
        throw new NoSuchFieldException(type.getName() + "." + name);
    }
    static Class<?> boxed(Class<?> type) {
        if (type == int.class) return Integer.class;
        if (type == long.class) return Long.class;
        if (type == boolean.class) return Boolean.class;
        if (type == float.class) return Float.class;
        return type;
    }
    static boolean matches(Class<?>[] types, Object[] args) {
        if (types.length != args.length) return false;
        for (int i = 0; i < types.length; ++i)
            if (args[i] == null ? types[i].isPrimitive() : !boxed(types[i]).isInstance(args[i])) return false;
        return true;
    }
    static Object call(Object target, String name, Object... args) throws Exception {
        Class<?> type = target instanceof Class ? (Class<?>) target : target.getClass();
        for (Class<?> c = type; c != null; c = c.getSuperclass()) {
            for (Method m : c.getDeclaredMethods()) {
                if (!m.getName().equals(name) || !matches(m.getParameterTypes(), args)) continue;
                m.setAccessible(true);
                try { return m.invoke(target instanceof Class ? null : target, args); }
                catch (InvocationTargetException e) {
                    Throwable cause = e.getCause();
                    if (cause instanceof Exception) throw (Exception) cause;
                    if (cause instanceof Error) throw (Error) cause;
                    throw e;
                }
            }
        }
        throw new NoSuchMethodException(type.getName() + "." + name);
    }
    static Object create(String name, Object... args) throws Exception {
        Class<?> type = Class.forName(name);
        for (Constructor<?> ctor : type.getDeclaredConstructors()) {
            if (!matches(ctor.getParameterTypes(), args)) continue;
            ctor.setAccessible(true);
            return ctor.newInstance(args);
        }
        throw new NoSuchMethodException(name + " constructor");
    }
}
