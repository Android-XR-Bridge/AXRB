package java.util.zip;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.NoSuchElementException;

/** Read access to a ZIP archive in the guest file system (parsed by the host). */
public class ZipFile implements java.io.Closeable {
    public static final int OPEN_READ = 0x1;
    public static final int OPEN_DELETE = 0x4;

    private final String name;
    private long handle;
    private String[] names;

    public ZipFile(String name) throws IOException {
        this(new File(name));
    }

    public ZipFile(File file) throws IOException {
        this(file, OPEN_READ);
    }

    public ZipFile(File file, int mode) throws IOException {
        name = file.getPath();
        handle = nativeOpen(file.getAbsolutePath());
        if (handle == 0) throw new ZipException("error in opening zip file " + name);
    }

    private void ensureOpen() {
        if (handle == 0) throw new IllegalStateException("zip file closed");
    }

    public String getName() {
        return name;
    }

    public synchronized int size() {
        ensureOpen();
        return names().length;
    }

    private String[] names() {
        if (names == null) names = nativeNames(handle);
        return names;
    }

    public synchronized ZipEntry getEntry(String entryName) {
        if (entryName == null) throw new NullPointerException("name");
        ensureOpen();
        long[] info = nativeEntry(handle, entryName);
        if (info == null && !entryName.endsWith("/")) {
            info = nativeEntry(handle, entryName + "/");
            if (info != null) entryName = entryName + "/";
        }
        if (info == null) return null;
        ZipEntry e = new ZipEntry(entryName);
        e.size = info[0];
        e.csize = info[1];
        e.method = (int) info[2];
        e.crc = info[3];
        e.time = info[4];
        e.dataOffset = info[5];
        return e;
    }

    public synchronized InputStream getInputStream(ZipEntry entry) throws IOException {
        ensureOpen();
        byte[] data = nativeRead(handle, entry.getName());
        if (data == null) throw new ZipException("cannot read entry " + entry.getName());
        return new ByteArrayInputStream(data);
    }

    public synchronized Enumeration<? extends ZipEntry> entries() {
        ensureOpen();
        final String[] all = names();
        return new Enumeration<ZipEntry>() {
            private int next;

            public boolean hasMoreElements() {
                return next < all.length;
            }

            public ZipEntry nextElement() {
                if (next >= all.length) throw new NoSuchElementException();
                ZipEntry e = getEntry(all[next++]);
                return e != null ? e : new ZipEntry(all[next - 1]);
            }
        };
    }

    public synchronized void close() throws IOException {
        if (handle != 0) {
            nativeClose(handle);
            handle = 0;
        }
    }

    private static native long nativeOpen(String path);

    private static native String[] nativeNames(long handle);

    /** {size, compressedSize, method, crc, time, dataOffset} or null. */
    private static native long[] nativeEntry(long handle, String name);

    private static native byte[] nativeRead(long handle, String name);

    private static native void nativeClose(long handle);
}
