package java.lang;

import java.io.Serializable;

public final class Boolean implements Serializable, Comparable<Boolean> {
    public static final Boolean TRUE = new Boolean(true);
    public static final Boolean FALSE = new Boolean(false);
    @SuppressWarnings("unchecked")
    public static final Class<Boolean> TYPE = (Class<Boolean>) Class.getPrimitiveClass("boolean");

    private final boolean value;

    public Boolean(boolean value) {
        this.value = value;
    }

    public Boolean(String s) {
        this(parseBoolean(s));
    }

    public static boolean parseBoolean(String s) {
        return s != null && s.equalsIgnoreCase("true");
    }

    public static Boolean valueOf(boolean b) {
        return b ? TRUE : FALSE;
    }

    public static Boolean valueOf(String s) {
        return parseBoolean(s) ? TRUE : FALSE;
    }

    public static String toString(boolean b) {
        return b ? "true" : "false";
    }

    public static int compare(boolean x, boolean y) {
        return x == y ? 0 : (x ? 1 : -1);
    }

    public static int hashCode(boolean value) {
        return value ? 1231 : 1237;
    }

    public static boolean logicalAnd(boolean a, boolean b) {
        return a && b;
    }

    public static boolean logicalOr(boolean a, boolean b) {
        return a || b;
    }

    public static boolean logicalXor(boolean a, boolean b) {
        return a ^ b;
    }

    public static boolean getBoolean(String name) {
        return parseBoolean(System.getProperty(name));
    }

    public boolean booleanValue() {
        return value;
    }

    @Override
    public int compareTo(Boolean other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Boolean && ((Boolean) o).value == value;
    }

    @Override
    public int hashCode() {
        return hashCode(value);
    }

    @Override
    public String toString() {
        return toString(value);
    }
}
