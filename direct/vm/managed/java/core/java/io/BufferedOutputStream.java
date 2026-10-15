package java.io;

public class BufferedOutputStream extends FilterOutputStream {
    protected byte[] buf;
    protected int count;

    public BufferedOutputStream(OutputStream out) {
        this(out, 8192);
    }

    public BufferedOutputStream(OutputStream out, int size) {
        super(out);
        if (size <= 0) throw new IllegalArgumentException("Buffer size <= 0");
        buf = new byte[size];
    }

    private void flushBuffer() throws IOException {
        if (count > 0) {
            out.write(buf, 0, count);
            count = 0;
        }
    }

    public synchronized void write(int b) throws IOException {
        if (count >= buf.length) flushBuffer();
        buf[count++] = (byte) b;
    }

    public synchronized void write(byte[] b, int off, int len) throws IOException {
        InputStream.checkRange(b.length, off, len);
        if (len >= buf.length) {
            flushBuffer();
            out.write(b, off, len);
            return;
        }
        if (len > buf.length - count) flushBuffer();
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }

    public synchronized void flush() throws IOException {
        flushBuffer();
        out.flush();
    }
}
