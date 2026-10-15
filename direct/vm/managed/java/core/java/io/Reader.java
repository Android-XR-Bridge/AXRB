package java.io;

public abstract class Reader implements Readable, Closeable {
    protected Object lock;

    protected Reader() {
        this.lock = this;
    }

    protected Reader(Object lock) {
        if (lock == null) throw new NullPointerException();
        this.lock = lock;
    }

    public int read() throws IOException {
        char[] cb = new char[1];
        return read(cb, 0, 1) == -1 ? -1 : cb[0];
    }

    public int read(char[] cbuf) throws IOException {
        return read(cbuf, 0, cbuf.length);
    }

    public abstract int read(char[] cbuf, int off, int len) throws IOException;

    public long skip(long n) throws IOException {
        if (n < 0L) throw new IllegalArgumentException("skip value is negative");
        char[] buf = new char[(int) Math.min(n, 8192)];
        long r = n;
        while (r > 0) {
            int nc = read(buf, 0, (int) Math.min(r, buf.length));
            if (nc == -1) break;
            r -= nc;
        }
        return n - r;
    }

    public boolean ready() throws IOException {
        return false;
    }

    public boolean markSupported() {
        return false;
    }

    public void mark(int readAheadLimit) throws IOException {
        throw new IOException("mark() not supported");
    }

    public void reset() throws IOException {
        throw new IOException("reset() not supported");
    }

    public abstract void close() throws IOException;
}
