package java.io;

public abstract class InputStream implements Closeable {
    public InputStream() {}

    public abstract int read() throws IOException;

    public int read(byte[] b) throws IOException {
        return read(b, 0, b.length);
    }

    public int read(byte[] b, int off, int len) throws IOException {
        checkRange(b.length, off, len);
        if (len == 0) return 0;
        int c = read();
        if (c == -1) return -1;
        b[off] = (byte) c;
        int i = 1;
        for (; i < len; i++) {
            c = read();
            if (c == -1) break;
            b[off + i] = (byte) c;
        }
        return i;
    }

    public byte[] readAllBytes() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = read(buf, 0, buf.length)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }

    public long transferTo(OutputStream out) throws IOException {
        long total = 0;
        byte[] buf = new byte[8192];
        int n;
        while ((n = read(buf, 0, buf.length)) > 0) {
            out.write(buf, 0, n);
            total += n;
        }
        return total;
    }

    public long skip(long n) throws IOException {
        long remaining = n;
        byte[] buf = new byte[(int) Math.min(2048, Math.max(n, 1))];
        while (remaining > 0) {
            int r = read(buf, 0, (int) Math.min(buf.length, remaining));
            if (r < 0) break;
            remaining -= r;
        }
        return n - remaining;
    }

    public int available() throws IOException {
        return 0;
    }

    public void close() throws IOException {}

    public synchronized void mark(int readlimit) {}

    public synchronized void reset() throws IOException {
        throw new IOException("mark/reset not supported");
    }

    public boolean markSupported() {
        return false;
    }

    static void checkRange(int length, int off, int len) {
        if (off < 0 || len < 0 || len > length - off) {
            throw new IndexOutOfBoundsException("length=" + length + "; regionStart=" + off + "; regionLength=" + len);
        }
    }
}
