package java.io;

public class ByteArrayInputStream extends InputStream {
    protected byte[] buf;
    protected int pos;
    protected int mark;
    protected int count;

    public ByteArrayInputStream(byte[] buf) {
        this(buf, 0, buf.length);
    }

    public ByteArrayInputStream(byte[] buf, int offset, int length) {
        this.buf = buf;
        this.pos = offset;
        this.count = Math.min(offset + length, buf.length);
        this.mark = offset;
    }

    public synchronized int read() {
        return pos < count ? (buf[pos++] & 0xff) : -1;
    }

    public synchronized int read(byte[] b, int off, int len) {
        checkRange(b.length, off, len);
        if (pos >= count) return -1;
        int avail = count - pos;
        if (len > avail) len = avail;
        if (len <= 0) return 0;
        System.arraycopy(buf, pos, b, off, len);
        pos += len;
        return len;
    }

    public synchronized long skip(long n) {
        long k = Math.min(count - pos, Math.max(n, 0));
        pos += (int) k;
        return k;
    }

    public synchronized int available() {
        return count - pos;
    }

    public boolean markSupported() {
        return true;
    }

    public void mark(int readAheadLimit) {
        mark = pos;
    }

    public synchronized void reset() {
        pos = mark;
    }

    public void close() {}
}
