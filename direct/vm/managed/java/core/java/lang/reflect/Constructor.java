package java.lang.reflect;

public final class Constructor<T> extends AccessibleObject implements Member {
    private long vmMethod;
    private Class<T> declaringClass;
    private Class<?>[] parameterTypes;
    private int modifiers;

    private Constructor() {}

    @Override
    public Class<T> getDeclaringClass() {
        return declaringClass;
    }

    @Override
    public String getName() {
        return declaringClass.getName();
    }

    @Override
    public int getModifiers() {
        return modifiers & 0xffff;
    }

    @Override
    public boolean isSynthetic() {
        return (modifiers & 0x1000) != 0;
    }

    public boolean isVarArgs() {
        return (modifiers & 0x80) != 0;
    }

    public Class<?>[] getParameterTypes() {
        return parameterTypes.clone();
    }

    public int getParameterCount() {
        return parameterTypes.length;
    }

    public native T newInstance(Object... args)
            throws InstantiationException, IllegalAccessException, IllegalArgumentException,
                   InvocationTargetException;

    @Override
    public boolean equals(Object o) {
        return o instanceof Constructor && ((Constructor<?>) o).vmMethod == vmMethod;
    }

    @Override
    public int hashCode() {
        return declaringClass.getName().hashCode();
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        String mods = Modifier.toString(getModifiers());
        if (!mods.isEmpty()) sb.append(mods).append(' ');
        sb.append(declaringClass.getTypeName()).append('(');
        for (int i = 0; i < parameterTypes.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(parameterTypes[i].getTypeName());
        }
        return sb.append(')').toString();
    }
}
