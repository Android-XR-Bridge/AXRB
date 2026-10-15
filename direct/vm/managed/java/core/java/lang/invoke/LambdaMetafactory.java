package java.lang.invoke;

/** See MethodHandle: javac names it in lambda bootstrap methods; d8 removes those. */
public final class LambdaMetafactory {
    private LambdaMetafactory() {}

    public static CallSite metafactory(MethodHandles.Lookup caller, String invokedName, MethodType invokedType,
            MethodType samMethodType, MethodHandle implMethod, MethodType instantiatedMethodType) {
        throw MethodHandles.unsupported();
    }

    public static CallSite altMetafactory(MethodHandles.Lookup caller, String invokedName, MethodType invokedType,
            Object... args) {
        throw MethodHandles.unsupported();
    }
}
