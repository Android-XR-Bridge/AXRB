package java.lang;

import java.io.Serializable;
import java.io.UnsupportedEncodingException;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Formatter;
import java.util.Locale;

/**
 * Immutable UTF-16 string. The runtime relies on the exact field layout: `value`
 * holds the characters (never shared with mutable code) and `hash` caches
 * hashCode().
 */
public final class String implements Serializable, Comparable<String>, CharSequence {
    private final char[] value;
    private int hash;

    public static final Comparator<String> CASE_INSENSITIVE_ORDER = new Comparator<String>() {
        @Override
        public int compare(String a, String b) {
            return a.compareToIgnoreCase(b);
        }
    };

    public String() {
        value = new char[0];
    }

    public String(String original) {
        value = original.value;
        hash = original.hash;
    }

    public String(char[] chars) {
        this(chars, 0, chars.length);
    }

    public String(char[] chars, int offset, int count) {
        checkBounds(chars.length, offset, count);
        value = new char[count];
        System.arraycopy(chars, offset, value, 0, count);
    }

    public String(int[] codePoints, int offset, int count) {
        checkBounds(codePoints.length, offset, count);
        StringBuilder sb = new StringBuilder(count);
        for (int i = 0; i < count; i++) sb.appendCodePoint(codePoints[offset + i]);
        value = sb.toString().value;
    }

    public String(byte[] bytes) {
        this(bytes, 0, bytes.length);
    }

    public String(byte[] bytes, int offset, int length) {
        checkBounds(bytes.length, offset, length);
        value = Utf8.decode(bytes, offset, length);
    }

    public String(byte[] bytes, int offset, int length, String charsetName)
            throws UnsupportedEncodingException {
        this(bytes, offset, length, Charset.forNameOrThrowUnsupported(charsetName));
    }

    public String(byte[] bytes, String charsetName) throws UnsupportedEncodingException {
        this(bytes, 0, bytes.length, charsetName);
    }

    public String(byte[] bytes, Charset charset) {
        this(bytes, 0, bytes.length, charset);
    }

    public String(byte[] bytes, int offset, int length, Charset charset) {
        checkBounds(bytes.length, offset, length);
        value = charset.decodeToChars(bytes, offset, length);
    }

    public String(StringBuilder builder) {
        value = builder.toString().value;
    }

    public String(StringBuffer buffer) {
        value = buffer.toString().value;
    }

    /** Takes ownership of `chars`; used only inside the core library. */
    String(char[] chars, boolean share) {
        value = chars;
    }

    private static void checkBounds(int length, int offset, int count) {
        if (offset < 0 || count < 0 || offset > length - count) {
            throw new StringIndexOutOfBoundsException(
                    "length=" + length + "; regionStart=" + offset + "; regionLength=" + count);
        }
    }

    // --- CharSequence --------------------------------------------------------

    @Override
    public int length() {
        return value.length;
    }

    public boolean isEmpty() {
        return value.length == 0;
    }

    public boolean isBlank() {
        for (char c : value) {
            if (!Character.isWhitespace(c)) return false;
        }
        return true;
    }

    @Override
    public char charAt(int index) {
        if (index < 0 || index >= value.length) {
            throw new StringIndexOutOfBoundsException("length=" + value.length + "; index=" + index);
        }
        return value[index];
    }

    public int codePointAt(int index) {
        return Character.codePointAt(value, index);
    }

    public int codePointCount(int begin, int end) {
        return Character.codePointCount(this, begin, end);
    }

    @Override
    public CharSequence subSequence(int start, int end) {
        return substring(start, end);
    }

    @Override
    public String toString() {
        return this;
    }

    // --- comparison ----------------------------------------------------------

    @Override
    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof String)) return false;
        char[] a = value;
        char[] b = ((String) other).value;
        if (a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    public boolean equalsIgnoreCase(String other) {
        if (other == null || other.value.length != value.length) return false;
        return regionMatches(true, 0, other, 0, value.length);
    }

    public boolean contentEquals(CharSequence cs) {
        if (cs.length() != value.length) return false;
        for (int i = 0; i < value.length; i++) {
            if (value[i] != cs.charAt(i)) return false;
        }
        return true;
    }

    @Override
    public int hashCode() {
        int h = hash;
        if (h == 0 && value.length > 0) {
            for (char c : value) h = 31 * h + c;
            hash = h;
        }
        return h;
    }

    @Override
    public int compareTo(String other) {
        char[] a = value;
        char[] b = other.value;
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            if (a[i] != b[i]) return a[i] - b[i];
        }
        return a.length - b.length;
    }

    public int compareToIgnoreCase(String other) {
        char[] a = value;
        char[] b = other.value;
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            char c1 = a[i], c2 = b[i];
            if (c1 != c2) {
                c1 = Character.toLowerCase(Character.toUpperCase(c1));
                c2 = Character.toLowerCase(Character.toUpperCase(c2));
                if (c1 != c2) return c1 - c2;
            }
        }
        return a.length - b.length;
    }

    public boolean regionMatches(int toffset, String other, int ooffset, int len) {
        return regionMatches(false, toffset, other, ooffset, len);
    }

    public boolean regionMatches(boolean ignoreCase, int toffset, String other, int ooffset, int len) {
        if (ooffset < 0 || toffset < 0 || toffset > (long) value.length - len
                || ooffset > (long) other.value.length - len) {
            return false;
        }
        for (int i = 0; i < len; i++) {
            char c1 = value[toffset + i], c2 = other.value[ooffset + i];
            if (c1 == c2) continue;
            if (ignoreCase) {
                char u1 = Character.toUpperCase(c1), u2 = Character.toUpperCase(c2);
                if (u1 == u2 || Character.toLowerCase(u1) == Character.toLowerCase(u2)) continue;
            }
            return false;
        }
        return true;
    }

    public boolean startsWith(String prefix) {
        return startsWith(prefix, 0);
    }

    public boolean startsWith(String prefix, int offset) {
        return regionMatches(offset, prefix, 0, prefix.value.length);
    }

    public boolean endsWith(String suffix) {
        return regionMatches(value.length - suffix.value.length, suffix, 0, suffix.value.length);
    }

    // --- searching -----------------------------------------------------------

    public int indexOf(int ch) {
        return indexOf(ch, 0);
    }

    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        if (ch < Character.MIN_SUPPLEMENTARY_CODE_POINT) {
            for (int i = from; i < value.length; i++) {
                if (value[i] == ch) return i;
            }
            return -1;
        }
        return indexOf(new String(Character.toChars(ch), true), from);
    }

    public int lastIndexOf(int ch) {
        return lastIndexOf(ch, value.length - 1);
    }

    public int lastIndexOf(int ch, int from) {
        if (ch < Character.MIN_SUPPLEMENTARY_CODE_POINT) {
            for (int i = Math.min(from, value.length - 1); i >= 0; i--) {
                if (value[i] == ch) return i;
            }
            return -1;
        }
        return lastIndexOf(new String(Character.toChars(ch), true), from);
    }

    public int indexOf(String str) {
        return indexOf(str, 0);
    }

    public int indexOf(String str, int from) {
        char[] s = str.value;
        if (from < 0) from = 0;
        int max = value.length - s.length;
        outer:
        for (int i = from; i <= max; i++) {
            for (int j = 0; j < s.length; j++) {
                if (value[i + j] != s[j]) continue outer;
            }
            return i;
        }
        return -1;
    }

    public int lastIndexOf(String str) {
        return lastIndexOf(str, value.length);
    }

    public int lastIndexOf(String str, int from) {
        char[] s = str.value;
        int start = Math.min(from, value.length - s.length);
        outer:
        for (int i = start; i >= 0; i--) {
            for (int j = 0; j < s.length; j++) {
                if (value[i + j] != s[j]) continue outer;
            }
            return i;
        }
        return -1;
    }

    public boolean contains(CharSequence s) {
        return indexOf(s.toString()) >= 0;
    }

    // --- derived strings -----------------------------------------------------

    public String substring(int begin) {
        return substring(begin, value.length);
    }

    public String substring(int begin, int end) {
        if (begin < 0 || end > value.length || begin > end) {
            throw new StringIndexOutOfBoundsException(
                    "length=" + value.length + "; begin=" + begin + "; end=" + end);
        }
        if (begin == 0 && end == value.length) return this;
        char[] out = new char[end - begin];
        System.arraycopy(value, begin, out, 0, out.length);
        return new String(out, true);
    }

    public String concat(String str) {
        if (str.value.length == 0) return this;
        char[] out = new char[value.length + str.value.length];
        System.arraycopy(value, 0, out, 0, value.length);
        System.arraycopy(str.value, 0, out, value.length, str.value.length);
        return new String(out, true);
    }

    public String replace(char oldChar, char newChar) {
        if (oldChar == newChar) return this;
        char[] out = null;
        for (int i = 0; i < value.length; i++) {
            if (value[i] == oldChar) {
                if (out == null) out = value.clone();
                out[i] = newChar;
            }
        }
        return out == null ? this : new String(out, true);
    }

    public String replace(CharSequence target, CharSequence replacement) {
        String t = target.toString();
        String r = replacement.toString();
        if (t.isEmpty()) {
            StringBuilder sb = new StringBuilder(value.length + (value.length + 1) * r.length());
            sb.append(r);
            for (char c : value) {
                sb.append(c).append(r);
            }
            return sb.toString();
        }
        int i = indexOf(t);
        if (i < 0) return this;
        StringBuilder sb = new StringBuilder(value.length);
        int last = 0;
        while (i >= 0) {
            sb.append(value, last, i - last).append(r);
            last = i + t.length();
            i = indexOf(t, last);
        }
        sb.append(value, last, value.length - last);
        return sb.toString();
    }

    public String trim() {
        int start = 0, end = value.length;
        while (start < end && value[start] <= ' ') start++;
        while (end > start && value[end - 1] <= ' ') end--;
        return substring(start, end);
    }

    public String strip() {
        int start = 0, end = value.length;
        while (start < end && Character.isWhitespace(value[start])) start++;
        while (end > start && Character.isWhitespace(value[end - 1])) end--;
        return substring(start, end);
    }

    public String repeat(int count) {
        if (count < 0) throw new IllegalArgumentException("count is negative: " + count);
        StringBuilder sb = new StringBuilder(value.length * count);
        for (int i = 0; i < count; i++) sb.append(this);
        return sb.toString();
    }

    public String toLowerCase() {
        return toLowerCase(Locale.getDefault());
    }

    public String toLowerCase(Locale locale) {
        char[] out = null;
        for (int i = 0; i < value.length; i++) {
            char c = value[i];
            char l = Character.toLowerCase(c);
            if (l != c) {
                if (out == null) out = value.clone();
                out[i] = l;
            }
        }
        return out == null ? this : new String(out, true);
    }

    public String toUpperCase() {
        return toUpperCase(Locale.getDefault());
    }

    public String toUpperCase(Locale locale) {
        StringBuilder sb = null;
        for (int i = 0; i < value.length; i++) {
            char c = value[i];
            if (c == '\u00df') {  // sharp s has no single-char uppercase
                if (sb == null) sb = new StringBuilder(value.length + 1).append(value, 0, i);
                sb.append("SS");
                continue;
            }
            char u = Character.toUpperCase(c);
            if (sb == null && u != c) sb = new StringBuilder(value.length).append(value, 0, i);
            if (sb != null) sb.append(u);
        }
        return sb == null ? this : sb.toString();
    }

    public char[] toCharArray() {
        return value.clone();
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > value.length || srcBegin > srcEnd) {
            throw new StringIndexOutOfBoundsException("begin " + srcBegin + ", end " + srcEnd);
        }
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public byte[] getBytes() {
        return Utf8.encode(value, 0, value.length);
    }

    public byte[] getBytes(String charsetName) throws UnsupportedEncodingException {
        return Charset.forNameOrThrowUnsupported(charsetName).encodeChars(value, 0, value.length);
    }

    public byte[] getBytes(Charset charset) {
        return charset.encodeChars(value, 0, value.length);
    }

    /**
     * Splits around a regular expression. Only patterns without regex
     * metacharacters (or a single escaped/bracketed character) are supported;
     * anything else throws UnsupportedOperationException naming the pattern
     * rather than splitting incorrectly.
     */
    public String[] split(String regex) {
        return split(regex, 0);
    }

    public String[] split(String regex, int limit) {
        String literal = Utf8.literalOfSimpleRegex(regex);
        if (literal == null) {
            throw new UnsupportedOperationException(
                    "String.split: regular expression '" + regex + "' is not supported by this runtime yet");
        }
        ArrayList<String> parts = new ArrayList<String>();
        int start = 0;
        int i;
        while ((limit <= 0 || parts.size() < limit - 1) && (i = literal.isEmpty() ? -1 : indexOf(literal, start)) >= 0) {
            parts.add(substring(start, i));
            start = i + literal.length();
        }
        if (literal.isEmpty()) {
            for (int k = 0; k < value.length; k++) parts.add(String.valueOf(value[k]));
            return parts.toArray(new String[0]);
        }
        parts.add(substring(start));
        if (limit == 0) {
            int n = parts.size();
            while (n > 0 && parts.get(n - 1).isEmpty()) n--;
            while (parts.size() > n) parts.remove(parts.size() - 1);
        }
        return parts.toArray(new String[0]);
    }

    public boolean matches(String regex) {
        String literal = Utf8.literalOfSimpleRegex(regex);
        if (literal == null) {
            throw new UnsupportedOperationException(
                    "String.matches: regular expression '" + regex + "' is not supported by this runtime yet");
        }
        return equals(literal);
    }

    public String replaceAll(String regex, String replacement) {
        String literal = Utf8.literalOfSimpleRegex(regex);
        if (literal == null || replacement.indexOf('$') >= 0 || replacement.indexOf('\\') >= 0) {
            throw new UnsupportedOperationException(
                    "String.replaceAll: regular expression '" + regex + "' is not supported by this runtime yet");
        }
        return replace(literal, replacement);
    }

    public native String intern();

    // --- static helpers ------------------------------------------------------

    public static String valueOf(Object obj) {
        return obj == null ? "null" : obj.toString();
    }

    public static String valueOf(char[] data) {
        return new String(data);
    }

    public static String valueOf(char[] data, int offset, int count) {
        return new String(data, offset, count);
    }

    public static String copyValueOf(char[] data) {
        return new String(data);
    }

    public static String valueOf(boolean b) {
        return b ? "true" : "false";
    }

    public static String valueOf(char c) {
        return new String(new char[] {c}, true);
    }

    public static String valueOf(int i) {
        return Integer.toString(i);
    }

    public static String valueOf(long l) {
        return Long.toString(l);
    }

    public static String valueOf(float f) {
        return Float.toString(f);
    }

    public static String valueOf(double d) {
        return Double.toString(d);
    }

    public static String join(CharSequence delimiter, CharSequence... elements) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < elements.length; i++) {
            if (i > 0) sb.append(delimiter);
            sb.append(elements[i]);
        }
        return sb.toString();
    }

    public static String join(CharSequence delimiter, Iterable<? extends CharSequence> elements) {
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (CharSequence cs : elements) {
            if (!first) sb.append(delimiter);
            sb.append(cs);
            first = false;
        }
        return sb.toString();
    }

    public static String format(String format, Object... args) {
        return new Formatter().format(format, args).toString();
    }

    public static String format(Locale locale, String format, Object... args) {
        return new Formatter(locale).format(format, args).toString();
    }

    /** Characters without a copy; core-library internal. */
    char[] rawChars() {
        return value;
    }
}
