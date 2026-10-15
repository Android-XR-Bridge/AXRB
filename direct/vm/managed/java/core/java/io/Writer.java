package java.io;

public abstract class Writer implements Appendable, Closeable, Flushable {
    protected Object lock;

    protected Writer() {
        this.lock = this;
    }

    protected Writer(Object lock) {
        if (lock == null) throw new NullPointerException();
        this.lock = lock;
    }

    public void write(int c) throws IOException {
        write(new char[] {(char) c}, 0, 1);
    }

    public void write(char[] cbuf) throws IOException {
        write(cbuf, 0, cbuf.length);
    }

    public abstract void write(char[] cbuf, int off, int len) throws IOException;

    public void write(String str) throws IOException {
        write(str, 0, str.length());
    }

    public void write(String str, int off, int len) throws IOException {
        char[] c = new char[len];
        str.getChars(off, off + len, c, 0);
        write(c, 0, len);
    }

    public Writer append(CharSequence csq) throws IOException {
        write(String.valueOf(csq));
        return this;
    }

    public Writer append(CharSequence csq, int start, int end) throws IOException {
        if (csq == null) csq = "null";
        return append(csq.subSequence(start, end));
    }

    public Writer append(char c) throws IOException {
        write(c);
        return this;
    }

    public abstract void flush() throws IOException;

    public abstract void close() throws IOException;
}
