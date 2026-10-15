package java.io;

public class BufferedReader extends Reader {
    private final Reader in;
    private char[] cb;
    private int pos;
    private int limit;
    private boolean skipLF;
    private int markPos = -1;
    private int markLimit;

    public BufferedReader(Reader in) {
        this(in, 8192);
    }

    public BufferedReader(Reader in, int sz) {
        super(in);
        if (sz <= 0) throw new IllegalArgumentException("Buffer size <= 0");
        this.in = in;
        this.cb = new char[sz];
    }

    private boolean fill() throws IOException {
        if (markPos >= 0 && limit - markPos < markLimit) {
            // Keep the marked region; grow if needed.
            int keep = limit - markPos;
            if (markLimit > cb.length) cb = java.util.Arrays.copyOf(cb, markLimit);
            System.arraycopy(cb, markPos, cb, 0, keep);
            pos -= markPos;
            limit = keep;
            markPos = 0;
        } else {
            markPos = -1;
            pos = limit = 0;
        }
        int n;
        do {
            n = in.read(cb, limit, cb.length - limit);
        } while (n == 0);
        if (n < 0) return false;
        limit += n;
        return true;
    }

    public int read() throws IOException {
        synchronized (lock) {
            for (;;) {
                if (pos >= limit && !fill()) return -1;
                char c = cb[pos++];
                if (skipLF) {
                    skipLF = false;
                    if (c == '\n') continue;
                }
                return c;
            }
        }
    }

    public int read(char[] cbuf, int off, int len) throws IOException {
        InputStream.checkRange(cbuf.length, off, len);
        synchronized (lock) {
            if (len == 0) return 0;
            if (pos >= limit && !fill()) return -1;
            if (skipLF) {
                skipLF = false;
                if (cb[pos] == '\n') {
                    pos++;
                    if (pos >= limit && !fill()) return -1;
                }
            }
            int n = Math.min(len, limit - pos);
            System.arraycopy(cb, pos, cbuf, off, n);
            pos += n;
            return n;
        }
    }

    /** A line ends at '\n', '\r' or "\r\n"; the terminator is not returned. */
    public String readLine() throws IOException {
        synchronized (lock) {
            StringBuilder sb = null;
            for (;;) {
                if (pos >= limit && !fill()) {
                    return sb != null && sb.length() > 0 ? sb.toString() : null;
                }
                if (skipLF) {
                    skipLF = false;
                    if (cb[pos] == '\n') {
                        pos++;
                        continue;
                    }
                }
                int start = pos;
                while (pos < limit) {
                    char c = cb[pos];
                    if (c == '\n' || c == '\r') {
                        if (sb == null) sb = new StringBuilder();
                        sb.append(cb, start, pos - start);
                        pos++;
                        if (c == '\r') skipLF = true;
                        return sb.toString();
                    }
                    pos++;
                }
                if (sb == null) sb = new StringBuilder();
                sb.append(cb, start, pos - start);
            }
        }
    }

    public boolean ready() throws IOException {
        synchronized (lock) {
            return pos < limit || in.ready();
        }
    }

    public boolean markSupported() {
        return true;
    }

    public void mark(int readAheadLimit) throws IOException {
        if (readAheadLimit < 0) throw new IllegalArgumentException("Read-ahead limit < 0");
        synchronized (lock) {
            markPos = pos;
            markLimit = readAheadLimit;
        }
    }

    public void reset() throws IOException {
        synchronized (lock) {
            if (markPos < 0) throw new IOException("Stream not marked");
            pos = markPos;
        }
    }

    public void close() throws IOException {
        in.close();
    }
}
