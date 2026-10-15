package java.lang;

/** UTF-8 codec and small string helpers shared inside java.lang. */
final class Utf8 {
    private Utf8() {}

    /**
     * Decodes UTF-8; malformed sequences become U+FFFD, as Android does. Every
     * input byte yields at most one UTF-16 unit (a 4-byte sequence yields two),
     * so `len` chars always suffice.
     */
    static char[] decode(byte[] b, int off, int len) {
        char[] out = new char[len];
        int n = 0;
        int i = off;
        int end = off + len;
        while (i < end) {
            int c = b[i] & 0xff;
            if (c < 0x80) {
                out[n++] = (char) c;
                i++;
                continue;
            }
            int need;
            int cp;
            int min;
            if ((c & 0xe0) == 0xc0) {
                need = 1;
                cp = c & 0x1f;
                min = 0x80;
            } else if ((c & 0xf0) == 0xe0) {
                need = 2;
                cp = c & 0x0f;
                min = 0x800;
            } else if ((c & 0xf8) == 0xf0) {
                need = 3;
                cp = c & 0x07;
                min = 0x10000;
            } else {
                out[n++] = '\ufffd';
                i++;
                continue;
            }
            int j = 1;
            boolean ok = true;
            for (; j <= need; j++) {
                if (i + j >= end || (b[i + j] & 0xc0) != 0x80) {
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (b[i + j] & 0x3f);
            }
            if (!ok) {
                // Resynchronise at the byte that broke the sequence.
                out[n++] = '\ufffd';
                i += j;
                continue;
            }
            i += need + 1;
            if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                out[n++] = '\ufffd';  // overlong, out of range, or an encoded surrogate
            } else if (cp >= 0x10000) {
                out[n++] = Character.highSurrogate(cp);
                out[n++] = Character.lowSurrogate(cp);
            } else {
                out[n++] = (char) cp;
            }
        }
        if (n == out.length) return out;
        char[] trimmed = new char[n];
        System.arraycopy(out, 0, trimmed, 0, n);
        return trimmed;
    }

    static byte[] encode(char[] s, int off, int len) {
        int size = 0;
        for (int i = off; i < off + len; i++) {
            char c = s[i];
            if (c < 0x80) size += 1;
            else if (c < 0x800) size += 2;
            else if (Character.isHighSurrogate(c) && i + 1 < off + len && Character.isLowSurrogate(s[i + 1])) {
                size += 4;
                i++;
            } else if (Character.isSurrogate(c)) {
                size += 1;  // unpaired surrogate -> '?', as Android's encoder replaces it
            } else size += 3;
        }
        byte[] out = new byte[size];
        int n = 0;
        for (int i = off; i < off + len; i++) {
            char c = s[i];
            if (c < 0x80) {
                out[n++] = (byte) c;
            } else if (c < 0x800) {
                out[n++] = (byte) (0xc0 | (c >> 6));
                out[n++] = (byte) (0x80 | (c & 0x3f));
            } else if (Character.isHighSurrogate(c) && i + 1 < off + len && Character.isLowSurrogate(s[i + 1])) {
                int cp = Character.toCodePoint(c, s[++i]);
                out[n++] = (byte) (0xf0 | (cp >> 18));
                out[n++] = (byte) (0x80 | ((cp >> 12) & 0x3f));
                out[n++] = (byte) (0x80 | ((cp >> 6) & 0x3f));
                out[n++] = (byte) (0x80 | (cp & 0x3f));
            } else if (Character.isSurrogate(c)) {
                out[n++] = (byte) '?';
            } else {
                out[n++] = (byte) (0xe0 | (c >> 12));
                out[n++] = (byte) (0x80 | ((c >> 6) & 0x3f));
                out[n++] = (byte) (0x80 | (c & 0x3f));
            }
        }
        return out;
    }

    /**
     * If `regex` matches exactly one literal string (no metacharacters, or a
     * single escaped character such as "\\." or "[,]"), returns that literal;
     * otherwise null.
     */
    static String literalOfSimpleRegex(String regex) {
        final String meta = ".$|()[{^?*+\\";
        int n = regex.length();
        if (n == 2 && regex.charAt(0) == '\\' && !Character.isLetterOrDigit(regex.charAt(1))) {
            return String.valueOf(regex.charAt(1));
        }
        if (n == 3 && regex.charAt(0) == '[' && regex.charAt(2) == ']' && regex.charAt(1) != '^') {
            return String.valueOf(regex.charAt(1));
        }
        for (int i = 0; i < n; i++) {
            if (meta.indexOf(regex.charAt(i)) >= 0) return null;
        }
        return regex;
    }
}
