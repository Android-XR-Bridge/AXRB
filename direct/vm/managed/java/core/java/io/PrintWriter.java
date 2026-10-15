package java.io;

import java.util.Locale;

/** Never throws IOException; see checkError(). */
public class PrintWriter extends Writer {
    protected Writer out;
    private final boolean autoFlush;
    private boolean trouble;

    public PrintWriter(Writer out) {
        this(out, false);
    }

    public PrintWriter(Writer out, boolean autoFlush) {
        super(out);
        this.out = out;
        this.autoFlush = autoFlush;
    }

    public PrintWriter(OutputStream out) {
        this(out, false);
    }

    public PrintWriter(OutputStream out, boolean autoFlush) {
        this(new OutputStreamWriter(out), autoFlush);
    }

    public boolean checkError() {
        flush();
        return trouble;
    }

    protected void setError() {
        trouble = true;
    }

    public void flush() {
        synchronized (lock) {
            try {
                out.flush();
            } catch (IOException e) {
                trouble = true;
            }
        }
    }

    public void close() {
        synchronized (lock) {
            try {
                out.close();
            } catch (IOException e) {
                trouble = true;
            }
        }
    }

    public void write(int c) {
        synchronized (lock) {
            try {
                out.write(c);
            } catch (IOException e) {
                trouble = true;
            }
        }
    }

    public void write(char[] buf, int off, int len) {
        synchronized (lock) {
            try {
                out.write(buf, off, len);
            } catch (IOException e) {
                trouble = true;
            }
        }
    }

    public void write(char[] buf) {
        write(buf, 0, buf.length);
    }

    public void write(String s, int off, int len) {
        synchronized (lock) {
            try {
                out.write(s, off, len);
            } catch (IOException e) {
                trouble = true;
            }
        }
    }

    public void write(String s) {
        write(s, 0, s.length());
    }

    public void print(boolean b) {
        write(String.valueOf(b));
    }

    public void print(char c) {
        write(c);
    }

    public void print(int i) {
        write(String.valueOf(i));
    }

    public void print(long l) {
        write(String.valueOf(l));
    }

    public void print(float f) {
        write(String.valueOf(f));
    }

    public void print(double d) {
        write(String.valueOf(d));
    }

    public void print(char[] s) {
        write(s);
    }

    public void print(String s) {
        write(String.valueOf(s));
    }

    public void print(Object obj) {
        write(String.valueOf(obj));
    }

    public void println() {
        synchronized (lock) {
            write('\n');
            if (autoFlush) flush();
        }
    }

    public void println(boolean x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(char x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(int x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(long x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(float x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(double x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(char[] x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(String x) {
        synchronized (lock) {
            print(x);
            println();
        }
    }

    public void println(Object x) {
        String s = String.valueOf(x);
        synchronized (lock) {
            print(s);
            println();
        }
    }

    public PrintWriter printf(String format, Object... args) {
        return format(format, args);
    }

    public PrintWriter printf(Locale l, String format, Object... args) {
        return format(l, format, args);
    }

    public PrintWriter format(String format, Object... args) {
        write(String.format(format, args));
        if (autoFlush) flush();
        return this;
    }

    public PrintWriter format(Locale l, String format, Object... args) {
        write(String.format(l, format, args));
        if (autoFlush) flush();
        return this;
    }

    public PrintWriter append(CharSequence csq) {
        write(String.valueOf(csq));
        return this;
    }

    public PrintWriter append(CharSequence csq, int start, int end) {
        if (csq == null) csq = "null";
        return append(csq.subSequence(start, end));
    }

    public PrintWriter append(char c) {
        write(c);
        return this;
    }
}
