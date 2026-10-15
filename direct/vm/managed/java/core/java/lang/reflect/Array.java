package java.lang.reflect;

public final class Array {
    private Array() {}

    public static Object newInstance(Class<?> componentType, int length) throws NegativeArraySizeException {
        if (componentType == null) throw new NullPointerException("componentType == null");
        if (componentType == void.class) throw new IllegalArgumentException("Can't allocate an array of void");
        return nativeNewInstance(componentType, length);
    }

    public static Object newInstance(Class<?> componentType, int... dimensions) {
        if (dimensions.length == 0) throw new IllegalArgumentException("Empty dimensions array");
        return multi(componentType, dimensions, 0);
    }

    private static Object multi(Class<?> componentType, int[] dims, int level) {
        Class<?> type = componentType;
        for (int i = dims.length - 1; i > level; i--) type = nativeNewInstance(type, 0).getClass();
        Object array = nativeNewInstance(type, dims[level]);
        if (level + 1 < dims.length) {
            Object[] outer = (Object[]) array;
            for (int i = 0; i < outer.length; i++) outer[i] = multi(componentType, dims, level + 1);
        }
        return array;
    }

    public static native int getLength(Object array) throws IllegalArgumentException;

    public static Object get(Object array, int index) {
        if (array instanceof Object[]) return ((Object[]) array)[index];
        if (array instanceof int[]) return ((int[]) array)[index];
        if (array instanceof long[]) return ((long[]) array)[index];
        if (array instanceof byte[]) return ((byte[]) array)[index];
        if (array instanceof char[]) return ((char[]) array)[index];
        if (array instanceof short[]) return ((short[]) array)[index];
        if (array instanceof boolean[]) return ((boolean[]) array)[index];
        if (array instanceof float[]) return ((float[]) array)[index];
        if (array instanceof double[]) return ((double[]) array)[index];
        throw new IllegalArgumentException("Argument is not an array");
    }

    public static void set(Object array, int index, Object value) {
        if (array instanceof Object[]) {
            ((Object[]) array)[index] = value;
        } else if (array instanceof int[]) {
            ((int[]) array)[index] = ((Number) value).intValue();
        } else if (array instanceof long[]) {
            ((long[]) array)[index] = ((Number) value).longValue();
        } else if (array instanceof byte[]) {
            ((byte[]) array)[index] = (Byte) value;
        } else if (array instanceof char[]) {
            ((char[]) array)[index] = (Character) value;
        } else if (array instanceof short[]) {
            ((short[]) array)[index] = ((Number) value).shortValue();
        } else if (array instanceof boolean[]) {
            ((boolean[]) array)[index] = (Boolean) value;
        } else if (array instanceof float[]) {
            ((float[]) array)[index] = ((Number) value).floatValue();
        } else if (array instanceof double[]) {
            ((double[]) array)[index] = ((Number) value).doubleValue();
        } else {
            throw new IllegalArgumentException("Argument is not an array");
        }
    }

    private static native Object nativeNewInstance(Class<?> componentType, int length);
}
