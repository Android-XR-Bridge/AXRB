package java.io;

public class FileOutputStream extends OutputStream {
    private int handle;
    private boolean closed;
    private FileDescriptor fd;

    public FileOutputStream(FileDescriptor fdObj) {
        if (fdObj == null) throw new NullPointerException("fdObj == null");
        handle = fdObj.getInt$();
        fd = fdObj;
    }

    public final FileDescriptor getFD() throws IOException {
        if (fd == null) fd = new FileDescriptor(handle);
        return fd;
    }

    public FileOutputStream(String name) throws FileNotFoundException {
        this(name != null ? new File(name) : null, false);
    }

    public FileOutputStream(String name, boolean append) throws FileNotFoundException {
        this(name != null ? new File(name) : null, append);
    }

    public FileOutputStream(File file) throws FileNotFoundException {
        this(file, false);
    }

    public FileOutputStream(File file, boolean append) throws FileNotFoundException {
        if (file == null) throw new NullPointerException("file == null");
        int h = Vfs.open(file.getAbsolutePath(), true, append, true, false);
        if (h < 0) throw Vfs.openFailed(file.getPath(), -h);
        handle = h;
    }

    public void write(int b) throws IOException {
        write(new byte[] {(byte) b}, 0, 1);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        InputStream.checkRange(b.length, off, len);
        synchronized (this) {
            if (closed) throw new IOException("Stream Closed");
            int r = Vfs.write(handle, b, off, len);
            if (r < 0) throw new IOException("write failed: " + Vfs.strerror(-r));
        }
    }

    public synchronized void close() {
        if (closed) return;
        closed = true;
        Vfs.close(handle);
    }
}
