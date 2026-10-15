package java.io;

import java.util.Locale;

/** Prints text as UTF-8. Like OpenJDK's, it never throws IOException; see checkError(). */
public class PrintStream extends FilterOutputStream implements Appendable, Closeable {
    private final boolean autoFlush;
    private boolean trouble;

    public PrintStream(OutputStream out) {
        this(out, false);
    }

    public PrintStream(OutputStream out, boolean autoFlush) {
        super(out);
        if (out == null) throw new NullPointerException("out == null");
        this.autoFlush = autoFlush;
    }

    public PrintStream(OutputStream out, boolean autoFlush, String encoding) throws UnsupportedEncodingException {
        this(out, autoFlush);
        java.nio.charset.Charset c = java.nio.charset.Charset.forNameOrThrowUnsupported(encoding);
        if (c != java.nio.charset.StandardCharsets.UTF_8) {
            throw new UnsupportedEncodingException("PrintStream supports only UTF-8 here: " + encoding);
        }
    }

    public PrintStream(String fileName) throws FileNotFoundException {
        this(new FileOutputStream(fileName), false);
    }

    public PrintStream(File file) throws FileNotFoundException {
        this(new FileOutputStream(file), false);
    }

    public boolean checkError() {
        flush();
        return trouble;
    }

    protected void setError() {
        trouble = true;
    }

    protected void clearError() {
        trouble = false;
    }

    public synchronized void flush() {
        try {
            out.flush();
        } catch (IOException e) {
            trouble = true;
        }
    }

    public synchronized void close() {
        try {
            out.close();
        } catch (IOException e) {
            trouble = true;
        }
    }

    public synchronized void write(int b) {
        try {
            out.write(b);
            if (b == '\n' && autoFlush) out.flush();
        } catch (IOException e) {
            trouble = true;
        }
    }

    public synchronized void write(byte[] buf, int off, int len) {
        try {
            out.write(buf, off, len);
            if (autoFlush) out.flush();
        } catch (IOException e) {
            trouble = true;
        }
    }

    private synchronized void writeString(String s) {
        byte[] b = s.getBytes();
        try {
            out.write(b, 0, b.length);
            if (autoFlush && s.indexOf('\n') >= 0) out.flush();
        } catch (IOException e) {
            trouble = true;
        }
    }

    public void print(boolean b) {
        writeString(String.valueOf(b));
    }

    public void print(char c) {
        writeString(String.valueOf(c));
    }

    public void print(int i) {
        writeString(String.valueOf(i));
    }

    public void print(long l) {
        writeString(String.valueOf(l));
    }

    public void print(float f) {
        writeString(String.valueOf(f));
    }

    public void print(double d) {
        writeString(String.valueOf(d));
    }

    public void print(char[] s) {
        writeString(new String(s));
    }

    public void print(String s) {
        writeString(String.valueOf(s));
    }

    public void print(Object obj) {
        writeString(String.valueOf(obj));
    }

    public void println() {
        writeString("\n");
    }

    public void println(boolean x) {
        writeString(x + "\n");
    }

    public void println(char x) {
        writeString(x + "\n");
    }

    public void println(int x) {
        writeString(x + "\n");
    }

    public void println(long x) {
        writeString(x + "\n");
    }

    public void println(float x) {
        writeString(x + "\n");
    }

    public void println(double x) {
        writeString(x + "\n");
    }

    public void println(char[] x) {
        writeString(new String(x) + "\n");
    }

    public void println(String x) {
        writeString(x + "\n");
    }

    public void println(Object x) {
        writeString(String.valueOf(x) + "\n");
    }

    public PrintStream printf(String format, Object... args) {
        return format(format, args);
    }

    public PrintStream printf(Locale l, String format, Object... args) {
        return format(l, format, args);
    }

    public PrintStream format(String format, Object... args) {
        writeString(String.format(format, args));
        return this;
    }

    public PrintStream format(Locale l, String format, Object... args) {
        writeString(String.format(l, format, args));
        return this;
    }

    public PrintStream append(CharSequence csq) {
        print(String.valueOf(csq));
        return this;
    }

    public PrintStream append(CharSequence csq, int start, int end) {
        return append(csq == null ? "null".subSequence(start, end) : csq.subSequence(start, end));
    }

    public PrintStream append(char c) {
        print(c);
        return this;
    }
}
