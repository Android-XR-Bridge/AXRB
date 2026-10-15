package java.lang.invoke;

/** See MethodHandle: present for compilation only. */
public final class MethodType {
    private MethodType() {}

    public static MethodType methodType(Class<?> rtype, Class<?>[] ptypes) {
        throw MethodHandles.unsupported();
    }

    public static MethodType methodType(Class<?> rtype) {
        throw MethodHandles.unsupported();
    }
}
