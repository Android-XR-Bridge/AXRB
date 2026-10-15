package java.lang.invoke;

/** See MethodHandle: only reached by code compiled without -XDstringConcat=inline, which d8 desugars. */
public final class StringConcatFactory {
    private StringConcatFactory() {}

    public static CallSite makeConcatWithConstants(MethodHandles.Lookup lookup, String name, MethodType concatType,
            String recipe, Object... constants) {
        throw MethodHandles.unsupported();
    }
}
