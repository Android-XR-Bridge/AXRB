package java.lang;

/** Stack introspection used by the core library (caller-sensitive methods). */
final class VMStack {
    private VMStack() {}

    /** The class loader of the method that called the method calling this one. */
    static native ClassLoader getCallingClassLoader();
}
