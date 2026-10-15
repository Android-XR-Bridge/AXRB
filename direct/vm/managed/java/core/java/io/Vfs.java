package java.io;

/**
 * The runtime's guest file system (src/managed/src/natives_io.cc). Paths are
 * guest paths ("/data/user/0/<pkg>/..."), resolved against the runtime's mount
 * table; nothing outside a mount is reachable. Failures return a negative
 * errno-style code or null; the callers turn them into the exceptions and
 * messages Android produces ("<path>: open failed: ENOENT (No such file or
 * directory)").
 */
final class Vfs {
    private Vfs() {}

    static final int ACCESS_EXISTS = 0;
    static final int ACCESS_READ = 4;
    static final int ACCESS_WRITE = 2;

    /** Returns a handle >= 0, or -errno. */
    static native int open(String path, boolean write, boolean append, boolean create, boolean exclusive);

    /** Bytes read, 0 at end of file, or -errno. */
    static native int read(int handle, byte[] b, int off, int len);

    /** 0 or -errno. */
    static native int write(int handle, byte[] b, int off, int len);

    static native long available(int handle);

    static native long skip(int handle, long n);

    static native void close(int handle);

    static native boolean access(String path, int mode);

    static native boolean isDirectory(String path);

    static native boolean isFile(String path);

    static native long length(String path);

    static native long lastModified(String path);

    /** 0 or -errno. */
    static native int mkdir(String path);

    /** 0 or -errno. */
    static native int remove(String path);

    /** 0 or -errno. */
    static native int rename(String from, String to);

    /** Names of the entries of a directory, or null. */
    static native String[] list(String path);

    /** "ENOENT (No such file or directory)" for errno 2, etc. */
    static native String strerror(int errno);

    static FileNotFoundException openFailed(String path, int err) {
        return new FileNotFoundException(path + ": open failed: " + strerror(err));
    }
}
