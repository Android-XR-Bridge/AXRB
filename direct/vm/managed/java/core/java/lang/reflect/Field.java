package java.lang.reflect;

public final class Field extends AccessibleObject implements Member {
    private long vmField;
    private Class<?> declaringClass;
    private String name;
    private Class<?> type;
    private int modifiers;

    private Field() {}

    @Override
    public Class<?> getDeclaringClass() {
        return declaringClass;
    }

    @Override
    public String getName() {
        return name;
    }

    @Override
    public int getModifiers() {
        return modifiers & 0xffff;
    }

    @Override
    public boolean isSynthetic() {
        return (modifiers & 0x1000) != 0;
    }

    public boolean isEnumConstant() {
        return (modifiers & 0x4000) != 0;
    }

    public Class<?> getType() {
        return type;
    }

    /** Boxes primitive values. */
    public native Object get(Object obj) throws IllegalArgumentException, IllegalAccessException;

    /** Unboxes and widens as Java assignment conversion allows. */
    public native void set(Object obj, Object value) throws IllegalArgumentException, IllegalAccessException;

    public int getInt(Object obj) throws IllegalAccessException {
        return ((Number) get(obj)).intValue();
    }

    public long getLong(Object obj) throws IllegalAccessException {
        return ((Number) get(obj)).longValue();
    }

    public boolean getBoolean(Object obj) throws IllegalAccessException {
        return (Boolean) get(obj);
    }

    public void setInt(Object obj, int v) throws IllegalAccessException {
        set(obj, Integer.valueOf(v));
    }

    public void setLong(Object obj, long v) throws IllegalAccessException {
        set(obj, Long.valueOf(v));
    }

    public void setBoolean(Object obj, boolean v) throws IllegalAccessException {
        set(obj, Boolean.valueOf(v));
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Field && ((Field) o).vmField == vmField;
    }

    @Override
    public int hashCode() {
        return declaringClass.getName().hashCode() ^ name.hashCode();
    }

    @Override
    public String toString() {
        String mods = Modifier.toString(getModifiers());
        return (mods.isEmpty() ? "" : mods + " ") + type.getTypeName() + " " + declaringClass.getTypeName() + "." + name;
    }
}
