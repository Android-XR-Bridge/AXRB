package java.io;

public class BufferedInputStream extends FilterInputStream {
    protected volatile byte[] buf;
    protected int count;
    protected int pos;
    protected int markpos = -1;
    protected int marklimit;

    public BufferedInputStream(InputStream in) {
        this(in, 8192);
    }

    public BufferedInputStream(InputStream in, int size) {
        super(in);
        if (size <= 0) throw new IllegalArgumentException("Buffer size <= 0");
        buf = new byte[size];
    }

    private InputStream stream() throws IOException {
        InputStream s = in;
        if (s == null) throw new IOException("Stream closed");
        return s;
    }

    private void fill() throws IOException {
        if (markpos < 0) {
            pos = 0;
        } else if (pos >= buf.length) {
            if (markpos > 0) {
                int sz = pos - markpos;
                System.arraycopy(buf, markpos, buf, 0, sz);
                pos = sz;
                markpos = 0;
            } else if (buf.length >= marklimit) {
                markpos = -1;
                pos = 0;
            } else {
                buf = java.util.Arrays.copyOf(buf, Math.min(pos * 2, marklimit));
            }
        }
        count = pos;
        int n = stream().read(buf, pos, buf.length - pos);
        if (n > 0) count = n + pos;
    }

    public synchronized int read() throws IOException {
        if (pos >= count) {
            fill();
            if (pos >= count) return -1;
        }
        return buf[pos++] & 0xff;
    }

    public synchronized int read(byte[] b, int off, int len) throws IOException {
        checkRange(b.length, off, len);
        if (len == 0) return 0;
        int avail = count - pos;
        if (avail <= 0) {
            if (len >= buf.length && markpos < 0) return stream().read(b, off, len);
            fill();
            avail = count - pos;
            if (avail <= 0) return -1;
        }
        int n = Math.min(avail, len);
        System.arraycopy(buf, pos, b, off, n);
        pos += n;
        return n;
    }

    public synchronized long skip(long n) throws IOException {
        if (n <= 0) return 0;
        long avail = count - pos;
        if (avail <= 0) {
            if (markpos < 0) return stream().skip(n);
            fill();
            avail = count - pos;
            if (avail <= 0) return 0;
        }
        long skipped = Math.min(avail, n);
        pos += (int) skipped;
        return skipped;
    }

    public synchronized int available() throws IOException {
        return (count - pos) + stream().available();
    }

    public synchronized void mark(int readlimit) {
        marklimit = readlimit;
        markpos = pos;
    }

    public synchronized void reset() throws IOException {
        if (markpos < 0) throw new IOException("Resetting to invalid mark");
        pos = markpos;
    }

    public boolean markSupported() {
        return true;
    }

    public void close() throws IOException {
        InputStream s = in;
        in = null;
        if (s != null) s.close();
    }
}
