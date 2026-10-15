package java.io;

public class StringWriter extends Writer {
    private final StringBuffer buf;

    public StringWriter() {
        buf = new StringBuffer();
        lock = buf;
    }

    public StringWriter(int initialSize) {
        if (initialSize < 0) throw new IllegalArgumentException("Negative buffer size");
        buf = new StringBuffer(initialSize);
        lock = buf;
    }

    public void write(int c) {
        buf.append((char) c);
    }

    public void write(char[] cbuf, int off, int len) {
        InputStream.checkRange(cbuf.length, off, len);
        buf.append(cbuf, off, len);
    }

    public void write(String str) {
        buf.append(str);
    }

    public void write(String str, int off, int len) {
        buf.append(str, off, off + len);
    }

    public StringWriter append(CharSequence csq) {
        write(String.valueOf(csq));
        return this;
    }

    public StringWriter append(CharSequence csq, int start, int end) {
        if (csq == null) csq = "null";
        return append(csq.subSequence(start, end));
    }

    public StringWriter append(char c) {
        write(c);
        return this;
    }

    public String toString() {
        return buf.toString();
    }

    public StringBuffer getBuffer() {
        return buf;
    }

    public void flush() {}

    public void close() {}
}
