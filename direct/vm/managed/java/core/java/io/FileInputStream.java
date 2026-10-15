package java.io;

public class FileInputStream extends InputStream {
    private final String path;
    private int handle;
    private boolean closed;
    private FileDescriptor fd;

    public FileInputStream(FileDescriptor fdObj) {
        if (fdObj == null) throw new NullPointerException("fdObj == null");
        path = null;
        handle = fdObj.getInt$();
        fd = fdObj;
    }

    public final FileDescriptor getFD() throws IOException {
        if (fd == null) fd = new FileDescriptor(handle);
        return fd;
    }

    public FileInputStream(String name) throws FileNotFoundException {
        this(name != null ? new File(name) : null);
    }

    public FileInputStream(File file) throws FileNotFoundException {
        if (file == null) throw new NullPointerException("file == null");
        path = file.getAbsolutePath();
        int h = Vfs.open(path, false, false, false, false);
        if (h < 0) throw Vfs.openFailed(file.getPath(), -h);
        handle = h;
    }

    private void ensureOpen() throws IOException {
        if (closed) throw new IOException("Stream Closed");
    }

    public int read() throws IOException {
        byte[] b = new byte[1];
        int n = read(b, 0, 1);
        return n <= 0 ? -1 : b[0] & 0xff;
    }

    public int read(byte[] b, int off, int len) throws IOException {
        checkRange(b.length, off, len);
        synchronized (this) {
            ensureOpen();
            if (len == 0) return 0;
            int n = Vfs.read(handle, b, off, len);
            if (n < 0) throw new IOException("read failed: " + Vfs.strerror(-n));
            return n == 0 ? -1 : n;
        }
    }

    public synchronized long skip(long n) throws IOException {
        ensureOpen();
        if (n <= 0) return 0;
        long r = Vfs.skip(handle, n);
        if (r < 0) throw new IOException("lseek failed: " + Vfs.strerror((int) -r));
        return r;
    }

    public synchronized int available() throws IOException {
        ensureOpen();
        long a = Vfs.available(handle);
        return a > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) Math.max(a, 0);
    }

    public synchronized void close() {
        if (closed) return;
        closed = true;
        Vfs.close(handle);
    }
}
