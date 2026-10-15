package java.lang;

import java.io.Serializable;

/**
 * Character classification. ASCII/Latin-1 is decided here; other code points use
 * the host's Unicode tables through natives (Windows NLS), not a hand-rolled
 * approximation.
 */
public final class Character implements Serializable, Comparable<Character> {
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '\uffff';
    public static final char MIN_HIGH_SURROGATE = '\ud800';
    public static final char MAX_HIGH_SURROGATE = '\udbff';
    public static final char MIN_LOW_SURROGATE = '\udc00';
    public static final char MAX_LOW_SURROGATE = '\udfff';
    public static final char MIN_SURROGATE = MIN_HIGH_SURROGATE;
    public static final char MAX_SURROGATE = MAX_LOW_SURROGATE;
    public static final int MIN_SUPPLEMENTARY_CODE_POINT = 0x010000;
    public static final int MIN_CODE_POINT = 0x000000;
    public static final int MAX_CODE_POINT = 0x10ffff;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    @SuppressWarnings("unchecked")
    public static final Class<Character> TYPE = (Class<Character>) Class.getPrimitiveClass("char");

    // Bits returned by nativeClassify.
    private static final int LETTER = 1;
    private static final int DIGIT = 2;
    private static final int SPACE = 4;
    private static final int UPPER = 8;
    private static final int LOWER = 16;
    private static final int CONTROL = 32;

    private static final Character[] CACHE = new Character[128];

    private final char value;

    public Character(char value) {
        this.value = value;
    }

    public static Character valueOf(char c) {
        if (c < 128) {
            Character v = CACHE[c];
            if (v == null) {
                v = new Character(c);
                CACHE[c] = v;
            }
            return v;
        }
        return new Character(c);
    }

    public char charValue() {
        return value;
    }

    private static int classify(int cp) {
        if (cp < 0x80) {
            if (cp >= 'a' && cp <= 'z') return LETTER | LOWER;
            if (cp >= 'A' && cp <= 'Z') return LETTER | UPPER;
            if (cp >= '0' && cp <= '9') return DIGIT;
            if (cp == ' ' || (cp >= 0x09 && cp <= 0x0d) || (cp >= 0x1c && cp <= 0x1f)) {
                return cp < 0x20 ? SPACE | CONTROL : SPACE;
            }
            if (cp < 0x20 || cp == 0x7f) return CONTROL;
            return 0;
        }
        return nativeClassify(cp);
    }

    private static native int nativeClassify(int codePoint);

    private static native int nativeToUpper(int codePoint);

    private static native int nativeToLower(int codePoint);

    public static boolean isLetter(char c) {
        return isLetter((int) c);
    }

    public static boolean isLetter(int cp) {
        return (classify(cp) & LETTER) != 0;
    }

    public static boolean isDigit(char c) {
        return isDigit((int) c);
    }

    public static boolean isDigit(int cp) {
        return (classify(cp) & DIGIT) != 0;
    }

    public static boolean isLetterOrDigit(char c) {
        return isLetterOrDigit((int) c);
    }

    public static boolean isLetterOrDigit(int cp) {
        return (classify(cp) & (LETTER | DIGIT)) != 0;
    }

    public static boolean isAlphabetic(int cp) {
        return isLetter(cp);
    }

    public static boolean isUpperCase(char c) {
        return isUpperCase((int) c);
    }

    public static boolean isUpperCase(int cp) {
        return (classify(cp) & UPPER) != 0;
    }

    public static boolean isLowerCase(char c) {
        return isLowerCase((int) c);
    }

    public static boolean isLowerCase(int cp) {
        return (classify(cp) & LOWER) != 0;
    }

    /** Java whitespace: space separators except non-breaking ones, plus controls. */
    public static boolean isWhitespace(char c) {
        return isWhitespace((int) c);
    }

    public static boolean isWhitespace(int cp) {
        if (cp == 0x00a0 || cp == 0x2007 || cp == 0x202f) return false;
        return (classify(cp) & SPACE) != 0;
    }

    public static boolean isSpaceChar(char c) {
        return isSpaceChar((int) c);
    }

    public static boolean isSpaceChar(int cp) {
        return (classify(cp) & SPACE) != 0 && (classify(cp) & CONTROL) == 0;
    }

    public static boolean isISOControl(char c) {
        return isISOControl((int) c);
    }

    public static boolean isISOControl(int cp) {
        return (cp >= 0 && cp <= 0x1f) || (cp >= 0x7f && cp <= 0x9f);
    }

    public static boolean isJavaIdentifierStart(char c) {
        return isLetter(c) || c == '$' || c == '_';
    }

    public static boolean isJavaIdentifierPart(char c) {
        return isLetterOrDigit(c) || c == '$' || c == '_';
    }

    public static char toUpperCase(char c) {
        return (char) toUpperCase((int) c);
    }

    public static int toUpperCase(int cp) {
        if (cp < 0x80) return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
        return nativeToUpper(cp);
    }

    public static char toLowerCase(char c) {
        return (char) toLowerCase((int) c);
    }

    public static int toLowerCase(int cp) {
        if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
        return nativeToLower(cp);
    }

    public static char toTitleCase(char c) {
        return toUpperCase(c);
    }

    public static int digit(char c, int radix) {
        return digit((int) c, radix);
    }

    public static int digit(int cp, int radix) {
        if (radix < MIN_RADIX || radix > MAX_RADIX) return -1;
        int v;
        if (cp >= '0' && cp <= '9') v = cp - '0';
        else if (cp >= 'a' && cp <= 'z') v = cp - 'a' + 10;
        else if (cp >= 'A' && cp <= 'Z') v = cp - 'A' + 10;
        else if (cp >= 0xff21 && cp <= 0xff3a) v = cp - 0xff21 + 10;  // fullwidth A-Z
        else if (cp >= 0xff41 && cp <= 0xff5a) v = cp - 0xff41 + 10;  // fullwidth a-z
        else return -1;
        return v < radix ? v : -1;
    }

    public static int getNumericValue(char c) {
        int d = digit(c, MAX_RADIX);
        return d;
    }

    public static char forDigit(int digit, int radix) {
        if (digit >= radix || digit < 0 || radix < MIN_RADIX || radix > MAX_RADIX) return '\0';
        return (char) (digit < 10 ? '0' + digit : 'a' - 10 + digit);
    }

    // --- surrogates and code points -----------------------------------------

    public static boolean isHighSurrogate(char c) {
        return c >= MIN_HIGH_SURROGATE && c <= MAX_HIGH_SURROGATE;
    }

    public static boolean isLowSurrogate(char c) {
        return c >= MIN_LOW_SURROGATE && c <= MAX_LOW_SURROGATE;
    }

    public static boolean isSurrogate(char c) {
        return c >= MIN_SURROGATE && c <= MAX_SURROGATE;
    }

    public static boolean isSurrogatePair(char high, char low) {
        return isHighSurrogate(high) && isLowSurrogate(low);
    }

    public static boolean isValidCodePoint(int cp) {
        return cp >= MIN_CODE_POINT && cp <= MAX_CODE_POINT;
    }

    public static boolean isBmpCodePoint(int cp) {
        return cp >>> 16 == 0;
    }

    public static boolean isSupplementaryCodePoint(int cp) {
        return cp >= MIN_SUPPLEMENTARY_CODE_POINT && cp <= MAX_CODE_POINT;
    }

    public static int charCount(int cp) {
        return cp >= MIN_SUPPLEMENTARY_CODE_POINT ? 2 : 1;
    }

    public static int toCodePoint(char high, char low) {
        return ((high - MIN_HIGH_SURROGATE) << 10) + (low - MIN_LOW_SURROGATE) + MIN_SUPPLEMENTARY_CODE_POINT;
    }

    public static char highSurrogate(int cp) {
        return (char) ((cp >>> 10) + (MIN_HIGH_SURROGATE - (MIN_SUPPLEMENTARY_CODE_POINT >>> 10)));
    }

    public static char lowSurrogate(int cp) {
        return (char) ((cp & 0x3ff) + MIN_LOW_SURROGATE);
    }

    public static char[] toChars(int cp) {
        if (isBmpCodePoint(cp)) return new char[] {(char) cp};
        if (!isValidCodePoint(cp)) throw new IllegalArgumentException("Not a valid Unicode code point: 0x" + Integer.toHexString(cp));
        return new char[] {highSurrogate(cp), lowSurrogate(cp)};
    }

    public static int codePointAt(char[] a, int index) {
        return codePointAt(a, index, a.length);
    }

    public static int codePointAt(char[] a, int index, int limit) {
        if (index >= limit || index < 0) throw new IndexOutOfBoundsException("index " + index);
        char c1 = a[index];
        if (isHighSurrogate(c1) && index + 1 < limit && isLowSurrogate(a[index + 1])) {
            return toCodePoint(c1, a[index + 1]);
        }
        return c1;
    }

    public static int codePointAt(CharSequence seq, int index) {
        char c1 = seq.charAt(index);
        if (isHighSurrogate(c1) && index + 1 < seq.length() && isLowSurrogate(seq.charAt(index + 1))) {
            return toCodePoint(c1, seq.charAt(index + 1));
        }
        return c1;
    }

    public static int codePointCount(CharSequence seq, int begin, int end) {
        if (begin < 0 || end > seq.length() || begin > end) throw new IndexOutOfBoundsException();
        int n = end - begin;
        for (int i = begin; i < end - 1; i++) {
            if (isHighSurrogate(seq.charAt(i)) && isLowSurrogate(seq.charAt(i + 1))) {
                n--;
                i++;
            }
        }
        return n;
    }

    public static int compare(char x, char y) {
        return x - y;
    }

    public static int hashCode(char value) {
        return value;
    }

    public static char reverseBytes(char ch) {
        return (char) (((ch & 0xff00) >> 8) | (ch << 8));
    }

    public static String toString(char c) {
        return String.valueOf(c);
    }

    public static String toString(int codePoint) {
        return new String(toChars(codePoint));
    }

    @Override
    public int compareTo(Character other) {
        return compare(value, other.value);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Character && ((Character) o).value == value;
    }

    @Override
    public int hashCode() {
        return value;
    }

    @Override
    public String toString() {
        return String.valueOf(value);
    }
}
