package java.io;

public class BufferedWriter extends Writer {
    private Writer out;
    private final char[] cb;
    private int n;

    public BufferedWriter(Writer out) {
        this(out, 8192);
    }

    public BufferedWriter(Writer out, int sz) {
        super(out);
        if (sz <= 0) throw new IllegalArgumentException("Buffer size <= 0");
        this.out = out;
        this.cb = new char[sz];
    }

    private void ensureOpen() throws IOException {
        if (out == null) throw new IOException("Stream closed");
    }

    void flushBuffer() throws IOException {
        synchronized (lock) {
            ensureOpen();
            if (n == 0) return;
            out.write(cb, 0, n);
            n = 0;
        }
    }

    public void write(int c) throws IOException {
        synchronized (lock) {
            ensureOpen();
            if (n >= cb.length) flushBuffer();
            cb[n++] = (char) c;
        }
    }

    public void write(char[] cbuf, int off, int len) throws IOException {
        InputStream.checkRange(cbuf.length, off, len);
        synchronized (lock) {
            ensureOpen();
            if (len >= cb.length) {
                flushBuffer();
                out.write(cbuf, off, len);
                return;
            }
            if (len > cb.length - n) flushBuffer();
            System.arraycopy(cbuf, off, cb, n, len);
            n += len;
        }
    }

    public void newLine() throws IOException {
        write('\n');
    }

    public void flush() throws IOException {
        synchronized (lock) {
            flushBuffer();
            out.flush();
        }
    }

    public void close() throws IOException {
        synchronized (lock) {
            if (out == null) return;
            try (Writer w = out) {
                flushBuffer();
            } finally {
                out = null;
            }
        }
    }
}
