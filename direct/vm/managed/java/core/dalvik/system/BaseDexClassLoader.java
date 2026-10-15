package dalvik.system;

import java.io.File;

/**
 * A loader over DEX files: parent-first delegation, then its own DEX files in
 * order (ClassLoader.findClass). The runtime creates the application's loader
 * directly; loaders constructed from Java open their dex path through the VFS.
 * optimizedDirectory is ignored, as on Android 8+.
 */
public class BaseDexClassLoader extends ClassLoader {
    private final String dexPath;
    private final String librarySearchPath;

    public BaseDexClassLoader(String dexPath, File optimizedDirectory, String librarySearchPath, ClassLoader parent) {
        super(parent);
        if (dexPath == null) throw new NullPointerException("dexPath == null");
        this.dexPath = dexPath;
        this.librarySearchPath = librarySearchPath;
        nativeOpenDexPath(dexPath);
    }

    /** Runtime-only: the loader is already bound (vmLoader set) by the runtime. */
    BaseDexClassLoader(String dexPath, String librarySearchPath, ClassLoader parent, boolean runtimeCreated) {
        super(parent);
        this.dexPath = dexPath;
        this.librarySearchPath = librarySearchPath;
    }

    /** Opens every ':'-separated DEX, APK, JAR or ZIP and binds this loader to them. */
    private native void nativeOpenDexPath(String dexPath);

    /** Guest path of a native library in this loader's search path, or null. */
    public String findLibrary(String name) {
        if (librarySearchPath == null) return null;
        String file = System.mapLibraryName(name);
        for (String dir : librarySearchPath.split(":")) {
            if (dir.isEmpty()) continue;
            File f = new File(dir, file);
            if (f.isFile()) return f.getPath();
        }
        return null;
    }

    public String getLdLibraryPath() {
        return librarySearchPath == null ? "" : librarySearchPath;
    }

    @Override
    public String toString() {
        return getClass().getName() + "[DexPathList[[" + dexPath + "]]]";
    }
}
