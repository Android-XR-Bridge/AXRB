package java.nio.charset;

public final class StandardCharsets {
    private StandardCharsets() {}

    public static final Charset UTF_8 = new Utf8Charset();
    public static final Charset US_ASCII = new SingleByteCharset("US-ASCII",
            new String[] {"ASCII", "us", "ascii7", "646", "iso646-us", "ANSI_X3.4-1968"}, 0x7f);
    public static final Charset ISO_8859_1 = new SingleByteCharset("ISO-8859-1",
            new String[] {"ISO8859_1", "latin1", "l1", "8859_1", "ISO_8859-1", "cp819"}, 0xff);
    public static final Charset UTF_16BE =
            new Utf16Charset("UTF-16BE", new String[] {"UnicodeBigUnmarked", "X-UTF-16BE"}, true, false);
    public static final Charset UTF_16LE =
            new Utf16Charset("UTF-16LE", new String[] {"UnicodeLittleUnmarked", "X-UTF-16LE"}, false, false);
    public static final Charset UTF_16 = new Utf16Charset("UTF-16", new String[] {"UTF_16", "unicode", "utf16"}, true, true);

    private static final class Utf8Charset extends Charset {
        Utf8Charset() {
            super("UTF-8", new String[] {"UTF8", "unicode-1-1-utf-8"});
        }

        @Override
        public char[] decodeToChars(byte[] b, int off, int len) {
            return new String(b, off, len).toCharArray();
        }

        @Override
        public byte[] encodeChars(char[] c, int off, int len) {
            return new String(c, off, len).getBytes();
        }
    }

    private static final class SingleByteCharset extends Charset {
        private final int max;

        SingleByteCharset(String name, String[] aliases, int max) {
            super(name, aliases);
            this.max = max;
        }

        @Override
        public char[] decodeToChars(byte[] b, int off, int len) {
            char[] out = new char[len];
            for (int i = 0; i < len; i++) {
                int v = b[off + i] & 0xff;
                out[i] = v <= max ? (char) v : '\ufffd';
            }
            return out;
        }

        @Override
        public byte[] encodeChars(char[] c, int off, int len) {
            // A surrogate pair is one unmappable character and becomes one '?'.
            byte[] tmp = new byte[len];
            int n = 0;
            for (int i = 0; i < len; i++) {
                char ch = c[off + i];
                if (Character.isHighSurrogate(ch) && i + 1 < len && Character.isLowSurrogate(c[off + i + 1])) {
                    i++;
                    tmp[n++] = (byte) '?';
                    continue;
                }
                tmp[n++] = ch <= max ? (byte) ch : (byte) '?';
            }
            if (n == len) return tmp;
            byte[] out = new byte[n];
            System.arraycopy(tmp, 0, out, 0, n);
            return out;
        }
    }

    private static final class Utf16Charset extends Charset {
        private final boolean bigEndian;
        private final boolean bom;

        Utf16Charset(String name, String[] aliases, boolean bigEndian, boolean bom) {
            super(name, aliases);
            this.bigEndian = bigEndian;
            this.bom = bom;
        }

        @Override
        public char[] decodeToChars(byte[] b, int off, int len) {
            boolean be = bigEndian;
            if (bom && len >= 2) {
                int b0 = b[off] & 0xff, b1 = b[off + 1] & 0xff;
                if (b0 == 0xfe && b1 == 0xff) {
                    off += 2;
                    len -= 2;
                } else if (b0 == 0xff && b1 == 0xfe) {
                    be = false;
                    off += 2;
                    len -= 2;
                }
            }
            int units = len / 2;
            char[] out = new char[units + (len & 1)];
            for (int i = 0; i < units; i++) {
                int x = b[off + 2 * i] & 0xff, y = b[off + 2 * i + 1] & 0xff;
                out[i] = be ? (char) ((x << 8) | y) : (char) ((y << 8) | x);
            }
            if ((len & 1) != 0) out[units] = '\ufffd';
            for (int i = 0; i < units; i++) {  // unpaired surrogates are malformed
                char ch = out[i];
                if (Character.isHighSurrogate(ch)) {
                    if (i + 1 < units && Character.isLowSurrogate(out[i + 1])) {
                        i++;
                    } else {
                        out[i] = '\ufffd';
                    }
                } else if (Character.isLowSurrogate(ch)) {
                    out[i] = '\ufffd';
                }
            }
            return out;
        }

        @Override
        public byte[] encodeChars(char[] c, int off, int len) {
            int start = bom ? 2 : 0;
            byte[] out = new byte[start + 2 * len];
            if (bom) {
                out[0] = (byte) 0xfe;
                out[1] = (byte) 0xff;
            }
            for (int i = 0; i < len; i++) {
                char ch = c[off + i];
                if (Character.isHighSurrogate(ch) && i + 1 < len && Character.isLowSurrogate(c[off + i + 1])) {
                    put(out, start + 2 * i, ch);
                    i++;
                    put(out, start + 2 * i, c[off + i]);
                    continue;
                }
                if (Character.isSurrogate(ch)) ch = '\ufffd';
                put(out, start + 2 * i, ch);
            }
            return out;
        }

        private void put(byte[] out, int at, char ch) {
            if (bigEndian) {
                out[at] = (byte) (ch >> 8);
                out[at + 1] = (byte) ch;
            } else {
                out[at] = (byte) ch;
                out[at + 1] = (byte) (ch >> 8);
            }
        }
    }
}
