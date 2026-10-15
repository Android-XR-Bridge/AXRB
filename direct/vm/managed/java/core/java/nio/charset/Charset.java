package java.nio.charset;

import java.io.UnsupportedEncodingException;

/**
 * The character sets every Android device must provide (UTF-8, US-ASCII,
 * ISO-8859-1, UTF-16, UTF-16BE, UTF-16LE). Others are reported unsupported
 * rather than silently decoded as something else.
 *
 * Only whole-array decode/encode is provided (no CharsetDecoder); malformed
 * input is replaced with U+FFFD / '?' as String's constructors specify.
 */
public abstract class Charset implements Comparable<Charset> {
    private final String name;
    private final String[] aliases;

    protected Charset(String canonicalName, String[] aliases) {
        this.name = canonicalName;
        this.aliases = aliases == null ? new String[0] : aliases;
    }

    public final String name() {
        return name;
    }

    public String displayName() {
        return name;
    }

    public final java.util.Set<String> aliases() {
        java.util.HashSet<String> s = new java.util.HashSet<String>();
        for (String a : aliases) s.add(a);
        return java.util.Collections.unmodifiableSet(s);
    }

    public static Charset defaultCharset() {
        return StandardCharsets.UTF_8;
    }

    public static boolean isSupported(String charsetName) {
        return lookup(charsetName) != null;
    }

    public static Charset forName(String charsetName) {
        Charset c = lookup(charsetName);
        if (c == null) throw new UnsupportedCharsetException(charsetName);
        return c;
    }

    /** For String/InputStreamReader, which declare UnsupportedEncodingException. */
    public static Charset forNameOrThrowUnsupported(String charsetName) throws UnsupportedEncodingException {
        if (charsetName == null) throw new NullPointerException("charsetName == null");
        Charset c;
        try {
            c = lookup(charsetName);
        } catch (IllegalCharsetNameException e) {
            c = null;
        }
        if (c == null) throw new UnsupportedEncodingException(charsetName);
        return c;
    }

    private static Charset lookup(String charsetName) {
        if (charsetName == null) throw new IllegalArgumentException("charsetName == null");
        checkName(charsetName);
        Charset[] all = {StandardCharsets.UTF_8, StandardCharsets.US_ASCII, StandardCharsets.ISO_8859_1,
                StandardCharsets.UTF_16, StandardCharsets.UTF_16BE, StandardCharsets.UTF_16LE};
        for (Charset c : all) {
            if (c.name.equalsIgnoreCase(charsetName)) return c;
            for (String a : c.aliases) {
                if (a.equalsIgnoreCase(charsetName)) return c;
            }
        }
        return null;
    }

    private static void checkName(String s) {
        if (s.isEmpty()) throw new IllegalCharsetNameException(s);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || (i > 0 && (c == '-' || c == '+' || c == ':' || c == '_' || c == '.'));
            if (!ok) throw new IllegalCharsetNameException(s);
        }
    }

    /** Decodes bytes; malformed sequences become U+FFFD. */
    public abstract char[] decodeToChars(byte[] b, int off, int len);

    /** Encodes chars; unmappable characters become '?'. */
    public abstract byte[] encodeChars(char[] c, int off, int len);

    @Override
    public final int compareTo(Charset that) {
        return name.compareToIgnoreCase(that.name);
    }

    @Override
    public final boolean equals(Object o) {
        return o instanceof Charset && name.equals(((Charset) o).name);
    }

    @Override
    public final int hashCode() {
        return name.hashCode();
    }

    @Override
    public final String toString() {
        return name;
    }
}
