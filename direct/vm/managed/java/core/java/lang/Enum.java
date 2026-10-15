package java.lang;

import java.io.Serializable;

public abstract class Enum<E extends Enum<E>> implements Comparable<E>, Serializable {
    private final String name;
    private final int ordinal;

    protected Enum(String name, int ordinal) {
        this.name = name;
        this.ordinal = ordinal;
    }

    public final String name() {
        return name;
    }

    public final int ordinal() {
        return ordinal;
    }

    @Override
    public String toString() {
        return name;
    }

    @Override
    public final boolean equals(Object other) {
        return this == other;
    }

    @Override
    public final int hashCode() {
        return super.hashCode();
    }

    @Override
    protected final Object clone() throws CloneNotSupportedException {
        throw new CloneNotSupportedException();
    }

    @Override
    public final int compareTo(E o) {
        Enum<?> other = o;
        if (getDeclaringClass() != other.getDeclaringClass()) throw new ClassCastException();
        return ordinal - other.ordinal;
    }

    @SuppressWarnings("unchecked")
    public final Class<E> getDeclaringClass() {
        Class<?> c = getClass();
        Class<?> s = c.getSuperclass();
        return (Class<E>) (s == Enum.class ? c : s);
    }

    public static <T extends Enum<T>> T valueOf(Class<T> enumType, String name) {
        if (enumType == null) throw new NullPointerException("enumType == null");
        if (name == null) throw new NullPointerException("name == null");
        T[] values = enumType.getEnumConstantsShared();
        if (values == null) throw new IllegalArgumentException(enumType + " is not an enum type.");
        for (T v : values) {
            if (name.equals(v.name())) return v;
        }
        throw new IllegalArgumentException("No enum constant " + enumType.getCanonicalName() + "." + name);
    }
}
