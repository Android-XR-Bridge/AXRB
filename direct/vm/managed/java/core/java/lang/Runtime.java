package java.lang;

public class Runtime {
    private static final Runtime INSTANCE = new Runtime();

    private Runtime() {}

    public static Runtime getRuntime() {
        return INSTANCE;
    }

    public void exit(int status) {
        nativeExit(status);
    }

    public void halt(int status) {
        nativeExit(status);
    }

    public native void gc();

    public native long freeMemory();

    public native long totalMemory();

    public native long maxMemory();

    public native int availableProcessors();

    /** Process creation is not supported (spec 10.3: no fork/exec). */
    public Process exec(String command) throws java.io.IOException {
        throw new java.io.IOException("Cannot run program \"" + command + "\": process creation is not supported");
    }

    void loadLibrary0(ClassLoader loader, String libname) {
        if (libname.indexOf('/') >= 0) {
            throw new UnsatisfiedLinkError("Directory separator should not appear in library name: " + libname);
        }
        String error = nativeLoadLibrary(libname, loader);
        if (error != null) throw new UnsatisfiedLinkError(error);
    }

    void load0(ClassLoader loader, String filename) {
        String error = nativeLoad(filename, loader);
        if (error != null) throw new UnsatisfiedLinkError(error);
    }

    public void loadLibrary(String libname) {
        loadLibrary0(VMStack.getCallingClassLoader(), libname);
    }

    public void load(String filename) {
        load0(VMStack.getCallingClassLoader(), filename);
    }

    private static native void nativeExit(int status);

    /** Returns null on success, otherwise the reason (never a silent no-op). */
    private static native String nativeLoadLibrary(String libname, ClassLoader loader);

    private static native String nativeLoad(String filename, ClassLoader loader);
}
