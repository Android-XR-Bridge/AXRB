package java.lang;

/**
 * Class loaders are backed by the runtime (`vmLoader`). This runtime supports the
 * boot loader and dex-file loaders created by the runtime (PathClassLoader);
 * user-defined loaders that override findClass are consulted through
 * loadClass, but defineClass from bytes is not supported.
 */
public abstract class ClassLoader {
    private transient long vmLoader;
    private final ClassLoader parent;

    private static ClassLoader systemClassLoader;

    protected ClassLoader() {
        this(getSystemClassLoader());
    }

    protected ClassLoader(ClassLoader parent) {
        this.parent = parent;
    }

    public final ClassLoader getParent() {
        return parent;
    }

    public Class<?> loadClass(String name) throws ClassNotFoundException {
        return loadClass(name, false);
    }

    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        Class<?> c = findLoadedClass(name);
        if (c != null) return c;
        try {
            c = parent != null ? parent.loadClass(name, false) : BootClassLoader.INSTANCE.findClass(name);
        } catch (ClassNotFoundException e) {
            c = null;
        }
        if (c == null) c = findClass(name);
        return c;
    }

    protected Class<?> findClass(String name) throws ClassNotFoundException {
        Class<?> c = nativeFindClass(name);
        if (c == null) throw new ClassNotFoundException(name);
        return c;
    }

    protected String findLibrary(String libname) {
        return null;
    }

    protected final Class<?> findLoadedClass(String name) {
        return nativeFindLoadedClass(name);
    }

    protected final Class<?> defineClass(String name, byte[] b, int off, int len) {
        throw new UnsupportedOperationException("defineClass from class-file bytes is not supported on Android");
    }

    public static synchronized ClassLoader getSystemClassLoader() {
        return systemClassLoader != null ? systemClassLoader : BootClassLoader.INSTANCE;
    }

    /** Set by the runtime once the application class loader exists. */
    static synchronized void setSystemClassLoader(ClassLoader loader) {
        systemClassLoader = loader;
    }

    /** Looks only in this loader's own dex files (vmLoader). */
    private native Class<?> nativeFindClass(String name);

    private native Class<?> nativeFindLoadedClass(String name);
}

/** The boot class loader, as a Java object. */
final class BootClassLoader extends ClassLoader {
    static final BootClassLoader INSTANCE = new BootClassLoader();

    private BootClassLoader() {
        super(null);
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        return Class.forName(name, false, null);
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        return findClass(name);
    }
}
