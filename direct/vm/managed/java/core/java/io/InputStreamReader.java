package java.io;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

/**
 * Decodes incrementally: each chunk is cut at the last complete character so a
 * multi-byte sequence split across reads is decoded once it is whole.
 */
public class InputStreamReader extends Reader {
    private final InputStream in;
    private final Charset charset;
    private final byte[] bytes = new byte[8192];
    private int byteCount;          // undecoded bytes at the start of `bytes`
    private char[] chars = new char[0];
    private int charPos;
    private boolean eof;

    public InputStreamReader(InputStream in) {
        this(in, StandardCharsets.UTF_8);
    }

    public InputStreamReader(InputStream in, String charsetName) throws UnsupportedEncodingException {
        this(in, Charset.forNameOrThrowUnsupported(charsetName));
    }

    public InputStreamReader(InputStream in, Charset cs) {
        super(in);
        if (cs == null) throw new NullPointerException("charset == null");
        this.in = in;
        this.charset = cs;
    }

    public String getEncoding() {
        return charset.name();
    }

    /** Length of the prefix of bytes[0, n) made of complete characters. */
    private int completePrefix(int n) {
        if (charset == StandardCharsets.UTF_8) {
            // Look back at most 3 bytes for a lead byte whose sequence is cut off.
            for (int back = 1; back <= 3 && back <= n; back++) {
                int b = bytes[n - back] & 0xff;
                if ((b & 0xc0) == 0x80) continue;  // continuation byte
                int need = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
                return need > back ? n - back : n;
            }
            return n;
        }
        if (charset == StandardCharsets.UTF_16 || charset == StandardCharsets.UTF_16BE
                || charset == StandardCharsets.UTF_16LE) {
            return n & ~1;
        }
        return n;
    }

    private boolean fill() throws IOException {
        while (charPos >= chars.length) {
            if (eof) return false;
            int r = in.read(bytes, byteCount, bytes.length - byteCount);
            if (r < 0) {
                eof = true;
                if (byteCount == 0) return false;
                chars = charset.decodeToChars(bytes, 0, byteCount);  // malformed tail -> U+FFFD
                byteCount = 0;
            } else {
                int n = byteCount + r;
                int ok = completePrefix(n);
                chars = charset.decodeToChars(bytes, 0, ok);
                System.arraycopy(bytes, ok, bytes, 0, n - ok);
                byteCount = n - ok;
            }
            charPos = 0;
        }
        return true;
    }

    public int read(char[] cbuf, int off, int len) throws IOException {
        InputStream.checkRange(cbuf.length, off, len);
        synchronized (lock) {
            if (len == 0) return 0;
            if (!fill()) return -1;
            int n = Math.min(len, chars.length - charPos);
            System.arraycopy(chars, charPos, cbuf, off, n);
            charPos += n;
            return n;
        }
    }

    public boolean ready() throws IOException {
        synchronized (lock) {
            return charPos < chars.length || in.available() > 0;
        }
    }

    public void close() throws IOException {
        in.close();
    }
}
