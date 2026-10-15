package java.lang.reflect;

/** A method, backed by the runtime's method structure (`vmMethod`). */
public final class Method extends AccessibleObject implements Member {
    private long vmMethod;
    private Class<?> declaringClass;
    private String name;
    private Class<?>[] parameterTypes;
    private Class<?> returnType;
    private int modifiers;

    private Method() {}

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

    public boolean isVarArgs() {
        return (modifiers & 0x80) != 0;
    }

    public boolean isBridge() {
        return (modifiers & 0x40) != 0;
    }

    public boolean isDefault() {
        return declaringClass.isInterface() && (modifiers & (Modifier.ABSTRACT | Modifier.STATIC)) == 0;
    }

    public Class<?> getReturnType() {
        return returnType;
    }

    public Class<?>[] getParameterTypes() {
        return parameterTypes.clone();
    }

    public int getParameterCount() {
        return parameterTypes.length;
    }

    /**
     * Invokes the method. Instance methods dispatch virtually on `receiver`.
     * Exceptions thrown by the method are wrapped in InvocationTargetException.
     */
    /** Calls exactly this method (invokespecial), for MethodHandles.Lookup.unreflectSpecial. */
    public native Object invokeNonVirtual(Object receiver, Object[] args)
            throws IllegalAccessException, IllegalArgumentException, InvocationTargetException;

    public native Object invoke(Object receiver, Object... args)
            throws IllegalAccessException, IllegalArgumentException, InvocationTargetException;

    @Override
    public boolean equals(Object o) {
        return o instanceof Method && ((Method) o).vmMethod == vmMethod;
    }

    @Override
    public int hashCode() {
        return declaringClass.getName().hashCode() ^ name.hashCode();
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        String mods = Modifier.toString(getModifiers());
        if (!mods.isEmpty()) sb.append(mods).append(' ');
        sb.append(returnType.getTypeName()).append(' ').append(declaringClass.getTypeName()).append('.').append(name).append('(');
        for (int i = 0; i < parameterTypes.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(parameterTypes[i].getTypeName());
        }
        return sb.append(')').toString();
    }
}
