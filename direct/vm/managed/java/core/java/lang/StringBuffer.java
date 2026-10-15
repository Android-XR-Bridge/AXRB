package java.lang;

import java.io.Serializable;

/** Thread-safe variant of StringBuilder: every mutator is synchronized. */
public final class StringBuffer extends AbstractStringBuilder implements Serializable, Comparable<StringBuffer> {
    public StringBuffer() {
        super(16);
    }

    public StringBuffer(int capacity) {
        super(capacity);
    }

    public StringBuffer(String str) {
        super(str.length() + 16);
        append0(str);
    }

    public StringBuffer(CharSequence seq) {
        super(seq.length() + 16);
        append0(seq, 0, seq.length());
    }

    public synchronized StringBuffer append(Object obj) {
        append0(String.valueOf(obj));
        return this;
    }

    public synchronized StringBuffer append(String str) {
        append0(str);
        return this;
    }

    public synchronized StringBuffer append(StringBuilder sb) {
        append0(sb == null ? null : sb.toString());
        return this;
    }

    @Override
    public synchronized StringBuffer append(CharSequence s) {
        if (s == null) {
            appendNull();
        } else {
            append0(s, 0, s.length());
        }
        return this;
    }

    @Override
    public synchronized StringBuffer append(CharSequence s, int start, int end) {
        append0(s, start, end);
        return this;
    }

    public synchronized StringBuffer append(char[] str) {
        append0(str, 0, str.length);
        return this;
    }

    public synchronized StringBuffer append(char[] str, int offset, int len) {
        append0(str, offset, len);
        return this;
    }

    public synchronized StringBuffer append(boolean b) {
        append0(b ? "true" : "false");
        return this;
    }

    @Override
    public synchronized StringBuffer append(char c) {
        append0(c);
        return this;
    }

    public synchronized StringBuffer append(int i) {
        append0(Integer.toString(i));
        return this;
    }

    public synchronized StringBuffer append(long l) {
        append0(Long.toString(l));
        return this;
    }

    public synchronized StringBuffer append(float f) {
        append0(Float.toString(f));
        return this;
    }

    public synchronized StringBuffer append(double d) {
        append0(Double.toString(d));
        return this;
    }

    public synchronized StringBuffer appendCodePoint(int codePoint) {
        appendCodePoint0(codePoint);
        return this;
    }

    public synchronized StringBuffer delete(int start, int end) {
        delete0(start, end);
        return this;
    }

    public synchronized StringBuffer deleteCharAt(int index) {
        deleteCharAt0(index);
        return this;
    }

    public synchronized StringBuffer replace(int start, int end, String str) {
        replace0(start, end, str);
        return this;
    }

    public synchronized StringBuffer insert(int offset, String str) {
        insert0(offset, str);
        return this;
    }

    public synchronized StringBuffer insert(int offset, Object obj) {
        insert0(offset, String.valueOf(obj));
        return this;
    }

    public synchronized StringBuffer insert(int offset, char[] str) {
        insert0(offset, new String(str));
        return this;
    }

    public synchronized StringBuffer insert(int offset, CharSequence s) {
        insert0(offset, String.valueOf(s));
        return this;
    }

    public synchronized StringBuffer insert(int offset, boolean b) {
        insert0(offset, String.valueOf(b));
        return this;
    }

    public synchronized StringBuffer insert(int offset, char c) {
        insert0(offset, String.valueOf(c));
        return this;
    }

    public synchronized StringBuffer insert(int offset, int i) {
        insert0(offset, Integer.toString(i));
        return this;
    }

    public synchronized StringBuffer insert(int offset, long l) {
        insert0(offset, Long.toString(l));
        return this;
    }

    public synchronized StringBuffer insert(int offset, float f) {
        insert0(offset, Float.toString(f));
        return this;
    }

    public synchronized StringBuffer insert(int offset, double d) {
        insert0(offset, Double.toString(d));
        return this;
    }

    public synchronized StringBuffer reverse() {
        reverse0();
        return this;
    }

    @Override
    public synchronized int compareTo(StringBuffer other) {
        return toString().compareTo(other.toString());
    }

    @Override
    public synchronized String toString() {
        return new String(value, 0, count);
    }
}
