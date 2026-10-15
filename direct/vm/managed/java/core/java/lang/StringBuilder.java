package java.lang;

import java.io.Serializable;

public final class StringBuilder extends AbstractStringBuilder implements Serializable, Comparable<StringBuilder> {
    public StringBuilder() {
        super(16);
    }

    public StringBuilder(int capacity) {
        super(capacity);
    }

    public StringBuilder(String str) {
        super(str.length() + 16);
        append0(str);
    }

    public StringBuilder(CharSequence seq) {
        super(seq.length() + 16);
        append0(seq, 0, seq.length());
    }

    public StringBuilder append(Object obj) {
        append0(String.valueOf(obj));
        return this;
    }

    public StringBuilder append(String str) {
        append0(str);
        return this;
    }

    public StringBuilder append(StringBuffer sb) {
        append0(sb == null ? null : sb.toString());
        return this;
    }

    @Override
    public StringBuilder append(CharSequence s) {
        if (s == null) {
            appendNull();
        } else {
            append0(s, 0, s.length());
        }
        return this;
    }

    @Override
    public StringBuilder append(CharSequence s, int start, int end) {
        append0(s, start, end);
        return this;
    }

    public StringBuilder append(char[] str) {
        append0(str, 0, str.length);
        return this;
    }

    public StringBuilder append(char[] str, int offset, int len) {
        append0(str, offset, len);
        return this;
    }

    public StringBuilder append(boolean b) {
        append0(b ? "true" : "false");
        return this;
    }

    @Override
    public StringBuilder append(char c) {
        append0(c);
        return this;
    }

    public StringBuilder append(int i) {
        append0(Integer.toString(i));
        return this;
    }

    public StringBuilder append(long l) {
        append0(Long.toString(l));
        return this;
    }

    public StringBuilder append(float f) {
        append0(Float.toString(f));
        return this;
    }

    public StringBuilder append(double d) {
        append0(Double.toString(d));
        return this;
    }

    public StringBuilder appendCodePoint(int codePoint) {
        appendCodePoint0(codePoint);
        return this;
    }

    public StringBuilder delete(int start, int end) {
        delete0(start, end);
        return this;
    }

    public StringBuilder deleteCharAt(int index) {
        deleteCharAt0(index);
        return this;
    }

    public StringBuilder replace(int start, int end, String str) {
        replace0(start, end, str);
        return this;
    }

    public StringBuilder insert(int offset, String str) {
        insert0(offset, str);
        return this;
    }

    public StringBuilder insert(int offset, Object obj) {
        insert0(offset, String.valueOf(obj));
        return this;
    }

    public StringBuilder insert(int offset, char[] str) {
        insert0(offset, new String(str));
        return this;
    }

    public StringBuilder insert(int offset, CharSequence s) {
        insert0(offset, String.valueOf(s));
        return this;
    }

    public StringBuilder insert(int offset, boolean b) {
        insert0(offset, String.valueOf(b));
        return this;
    }

    public StringBuilder insert(int offset, char c) {
        insert0(offset, String.valueOf(c));
        return this;
    }

    public StringBuilder insert(int offset, int i) {
        insert0(offset, Integer.toString(i));
        return this;
    }

    public StringBuilder insert(int offset, long l) {
        insert0(offset, Long.toString(l));
        return this;
    }

    public StringBuilder insert(int offset, float f) {
        insert0(offset, Float.toString(f));
        return this;
    }

    public StringBuilder insert(int offset, double d) {
        insert0(offset, Double.toString(d));
        return this;
    }

    public StringBuilder reverse() {
        reverse0();
        return this;
    }

    @Override
    public int compareTo(StringBuilder other) {
        return toString().compareTo(other.toString());
    }

    @Override
    public String toString() {
        return new String(value, 0, count);
    }
}
