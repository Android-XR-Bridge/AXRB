package java.io;

public class FilterOutputStream extends OutputStream {
    protected OutputStream out;

    public FilterOutputStream(OutputStream out) {
        this.out = out;
    }

    public void write(int b) throws IOException {
        out.write(b);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        InputStream.checkRange(b.length, off, len);
        for (int i = 0; i < len; i++) write(b[off + i]);
    }

    public void flush() throws IOException {
        out.flush();
    }

    public void close() throws IOException {
        try (OutputStream o = out) {
            flush();
        }
    }
}
