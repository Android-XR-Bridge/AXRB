package java.io;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;
    private final Charset charset;
    private final StringBuilder pending = new StringBuilder();

    public OutputStreamWriter(OutputStream out) {
        this(out, StandardCharsets.UTF_8);
    }

    public OutputStreamWriter(OutputStream out, String charsetName) throws UnsupportedEncodingException {
        this(out, Charset.forNameOrThrowUnsupported(charsetName));
    }

    public OutputStreamWriter(OutputStream out, Charset cs) {
        super(out);
        if (cs == null) throw new NullPointerException("charset == null");
        this.out = out;
        this.charset = cs;
    }

    public String getEncoding() {
        return charset.name();
    }

    public void write(char[] cbuf, int off, int len) throws IOException {
        InputStream.checkRange(cbuf.length, off, len);
        synchronized (lock) {
            pending.append(cbuf, off, len);
            if (pending.length() >= 8192) drain(false);
        }
    }

    /** Encodes buffered chars, keeping a trailing high surrogate until its pair arrives. */
    private void drain(boolean all) throws IOException {
        int n = pending.length();
        if (!all && n > 0 && Character.isHighSurrogate(pending.charAt(n - 1))) n--;
        if (n == 0) return;
        char[] c = new char[n];
        pending.getChars(0, n, c, 0);
        pending.delete(0, n);
        byte[] b = charset.encodeChars(c, 0, n);
        out.write(b, 0, b.length);
    }

    public void flush() throws IOException {
        synchronized (lock) {
            drain(false);
            out.flush();
        }
    }

    public void close() throws IOException {
        synchronized (lock) {
            drain(true);
            out.close();
        }
    }
}
