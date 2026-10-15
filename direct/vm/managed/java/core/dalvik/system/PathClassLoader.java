package dalvik.system;

public class PathClassLoader extends BaseDexClassLoader {
    public PathClassLoader(String dexPath, ClassLoader parent) {
        super(dexPath, null, null, parent);
    }

    public PathClassLoader(String dexPath, String librarySearchPath, ClassLoader parent) {
        super(dexPath, null, librarySearchPath, parent);
    }

    /** Runtime-only (see BaseDexClassLoader). */
    private PathClassLoader(String dexPath, String librarySearchPath, ClassLoader parent, boolean runtimeCreated) {
        super(dexPath, librarySearchPath, parent, runtimeCreated);
    }
}
