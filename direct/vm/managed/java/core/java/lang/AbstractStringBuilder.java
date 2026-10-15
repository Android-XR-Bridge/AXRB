package java.lang;

/** Shared implementation of StringBuilder and StringBuffer. */
abstract class AbstractStringBuilder implements Appendable, CharSequence {
    char[] value;
    int count;

    AbstractStringBuilder(int capacity) {
        if (capacity < 0) throw new NegativeArraySizeException(Integer.toString(capacity));
        value = new char[capacity];
    }

    private void ensure(int min) {
        if (min > value.length) {
            int n = value.length * 2 + 2;
            if (n < min) n = min;
            char[] v = new char[n];
            System.arraycopy(value, 0, v, 0, count);
            value = v;
        }
    }

    @Override
    public int length() {
        return count;
    }

    public int capacity() {
        return value.length;
    }

    public void ensureCapacity(int min) {
        ensure(min);
    }

    public void trimToSize() {
        if (count < value.length) {
            char[] v = new char[count];
            System.arraycopy(value, 0, v, 0, count);
            value = v;
        }
    }

    public void setLength(int len) {
        if (len < 0) throw new StringIndexOutOfBoundsException(len);
        ensure(len);
        for (int i = count; i < len; i++) value[i] = 0;
        count = len;
    }

    @Override
    public char charAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ", length " + count);
        return value[index];
    }

    public int codePointAt(int index) {
        return Character.codePointAt(value, index, count);
    }

    public void setCharAt(int index, char c) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ", length " + count);
        value[index] = c;
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd) throw new StringIndexOutOfBoundsException(srcBegin);
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    AbstractStringBuilder appendNull() {
        return append0("null");
    }

    AbstractStringBuilder append0(String s) {
        if (s == null) return appendNull();
        char[] c = s.rawChars();
        ensure(count + c.length);
        System.arraycopy(c, 0, value, count, c.length);
        count += c.length;
        return this;
    }

    AbstractStringBuilder append0(char c) {
        ensure(count + 1);
        value[count++] = c;
        return this;
    }

    AbstractStringBuilder append0(char[] c, int offset, int len) {
        ensure(count + len);
        System.arraycopy(c, offset, value, count, len);
        count += len;
        return this;
    }

    AbstractStringBuilder append0(CharSequence s, int start, int end) {
        if (s == null) s = "null";
        if (start < 0 || start > end || end > s.length()) throw new IndexOutOfBoundsException("start " + start + ", end " + end);
        ensure(count + end - start);
        for (int i = start; i < end; i++) value[count++] = s.charAt(i);
        return this;
    }

    AbstractStringBuilder appendCodePoint0(int cp) {
        if (Character.isBmpCodePoint(cp)) return append0((char) cp);
        return append0(Character.toChars(cp), 0, 2);
    }

    AbstractStringBuilder insert0(int offset, String s) {
        if (offset < 0 || offset > count) throw new StringIndexOutOfBoundsException(offset);
        if (s == null) s = "null";
        char[] c = s.rawChars();
        ensure(count + c.length);
        System.arraycopy(value, offset, value, offset + c.length, count - offset);
        System.arraycopy(c, 0, value, offset, c.length);
        count += c.length;
        return this;
    }

    AbstractStringBuilder delete0(int start, int end) {
        if (end > count) end = count;
        if (start < 0 || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end);
        System.arraycopy(value, end, value, start, count - end);
        count -= end - start;
        return this;
    }

    AbstractStringBuilder deleteCharAt0(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException(index);
        return delete0(index, index + 1);
    }

    AbstractStringBuilder replace0(int start, int end, String str) {
        if (start < 0 || start > count || start > end) throw new StringIndexOutOfBoundsException(start);
        if (end > count) end = count;
        delete0(start, end);
        return insert0(start, str);
    }

    AbstractStringBuilder reverse0() {
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            char t = value[i];
            value[i] = value[j];
            value[j] = t;
        }
        // Re-pair surrogates that the swap reversed.
        for (int i = 0; i < count - 1; i++) {
            if (Character.isLowSurrogate(value[i]) && Character.isHighSurrogate(value[i + 1])) {
                char t = value[i];
                value[i] = value[i + 1];
                value[i + 1] = t;
                i++;
            }
        }
        return this;
    }

    public int indexOf(String str) {
        return indexOf(str, 0);
    }

    public int indexOf(String str, int from) {
        return toString().indexOf(str, from);
    }

    public int lastIndexOf(String str) {
        return toString().lastIndexOf(str);
    }

    public String substring(int start) {
        return substring(start, count);
    }

    public String substring(int start, int end) {
        if (start < 0 || end > count || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end);
        return new String(value, start, end - start);
    }

    @Override
    public CharSequence subSequence(int start, int end) {
        return substring(start, end);
    }

    @Override
    public String toString() {
        return new String(value, 0, count);
    }
}
