package java.lang;

import java.io.InputStream;
import java.io.PrintStream;

/**
 * System services. The standard streams go to the host log under the tags
 * "System.out" / "System.err", as they do to logcat on Android. There is no
 * standard input. getenv() exposes nothing from the host (spec 11).
 */
public final class System {
    public static final InputStream in = null;
    public static final PrintStream out = new PrintStream(new LogOutputStream(4, "System.out"), true);
    public static final PrintStream err = new PrintStream(new LogOutputStream(6, "System.err"), true);

    private System() {}

    public static native void arraycopy(Object src, int srcPos, Object dst, int dstPos, int length);

    public static native long currentTimeMillis();

    public static native long nanoTime();

    public static native int identityHashCode(Object x);

    public static String getProperty(String key) {
        if (key == null) throw new NullPointerException("key == null");
        if (key.isEmpty()) throw new IllegalArgumentException("key is empty");
        return nativeGetProperty(key);
    }

    public static String getProperty(String key, String def) {
        String v = getProperty(key);
        return v == null ? def : v;
    }

    public static String setProperty(String key, String value) {
        return nativeSetProperty(key, value);
    }

    private static final String[] KNOWN_PROPERTIES = {
        "file.encoding", "file.separator", "java.class.path", "java.io.tmpdir", "java.specification.version",
        "java.vm.name", "java.vm.version", "java.vendor", "java.version", "line.separator", "os.arch", "os.name",
        "os.version", "path.separator", "user.dir", "user.home", "user.language", "user.region", "http.agent",
        "java.boot.class.path", "java.library.path", "android.icu.library.version",
    };

    /** A snapshot of the VM's properties. */
    public static java.util.Properties getProperties() {
        java.util.Properties p = new java.util.Properties();
        for (String key : KNOWN_PROPERTIES) {
            String v = nativeGetProperty(key);
            if (v != null) p.setProperty(key, v);
        }
        return p;
    }

    public static String lineSeparator() {
        return "\n";
    }

    /** The host environment is not visible to applications. */
    public static String getenv(String name) {
        if (name == null) throw new NullPointerException("name == null");
        return null;
    }

    public static java.util.Map<String, String> getenv() {
        return java.util.Collections.emptyMap();
    }

    public static void exit(int status) {
        Runtime.getRuntime().exit(status);
    }

    public static void gc() {
        Runtime.getRuntime().gc();
    }

    public static void runFinalization() {}

    public static void loadLibrary(String libname) {
        Runtime.getRuntime().loadLibrary0(VMStack.getCallingClassLoader(), libname);
    }

    public static void load(String filename) {
        Runtime.getRuntime().load0(VMStack.getCallingClassLoader(), filename);
    }

    public static String mapLibraryName(String libname) {
        return "lib" + libname + ".so";
    }

    private static native String nativeGetProperty(String key);

    private static native String nativeSetProperty(String key, String value);

    /** Writes to the host log (android.util.Log priorities). Used by the streams. */
    static native void logNative(int priority, String tag, String message);

    /** Line-buffered stream that forwards complete lines to the host log. */
    private static final class LogOutputStream extends java.io.OutputStream {
        private final int priority;
        private final String tag;
        private final java.io.ByteArrayOutputStream line = new java.io.ByteArrayOutputStream();

        LogOutputStream(int priority, String tag) {
            this.priority = priority;
            this.tag = tag;
        }

        @Override
        public synchronized void write(int b) {
            if (b == '\n') {
                flushLine();
            } else {
                line.write(b);
            }
        }

        @Override
        public synchronized void flush() {
            if (line.size() > 0) flushLine();
        }

        private void flushLine() {
            logNative(priority, tag, new String(line.toByteArray()));
            line.reset();
        }
    }
}
