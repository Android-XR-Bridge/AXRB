package java.nio;

/**
 * Byte buffers over a byte[] (heap) or guest memory (direct). Direct buffers
 * are what JNI GetDirectBufferAddress exposes to native code, so their storage
 * lives in the guest address space.
 */
public abstract class ByteBuffer extends Buffer implements Comparable<ByteBuffer> {
    final byte[] hb;
    final int offset;
    boolean isReadOnly;
    boolean bigEndian = true;

    ByteBuffer(int mark, int pos, int lim, int cap, byte[] hb, int offset) {
        super(mark, pos, lim, cap);
        this.hb = hb;
        this.offset = offset;
    }

    public static ByteBuffer allocateDirect(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0: " + capacity);
        return new DirectByteBuffer(capacity);
    }

    public static ByteBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0: " + capacity);
        return new HeapByteBuffer(new byte[capacity], -1, 0, capacity, capacity, 0);
    }

    public static ByteBuffer wrap(byte[] array, int offset, int length) {
        checkBounds(offset, length, array.length);
        return new HeapByteBuffer(array, -1, offset, offset + length, array.length, 0);
    }

    public static ByteBuffer wrap(byte[] array) {
        return wrap(array, 0, array.length);
    }

    // --- element access: implemented by the heap and direct variants ----------------

    abstract byte getRaw(int index);

    abstract void putRaw(int index, byte b);

    public abstract ByteBuffer slice();

    public abstract ByteBuffer duplicate();

    public abstract ByteBuffer asReadOnlyBuffer();

    public abstract ByteBuffer compact();

    public byte get() {
        return getRaw(nextGetIndex(1));
    }

    public byte get(int index) {
        return getRaw(checkIndex(index, 1));
    }

    public ByteBuffer put(byte b) {
        checkWritable();
        putRaw(nextPutIndex(1), b);
        return this;
    }

    public ByteBuffer put(int index, byte b) {
        checkWritable();
        putRaw(checkIndex(index, 1), b);
        return this;
    }

    public ByteBuffer get(byte[] dst, int off, int length) {
        checkBounds(off, length, dst.length);
        if (length > remaining()) throw new BufferUnderflowException();
        getBytes(position, dst, off, length);
        position += length;
        return this;
    }

    public ByteBuffer get(byte[] dst) {
        return get(dst, 0, dst.length);
    }

    void getBytes(int index, byte[] dst, int off, int length) {
        for (int i = 0; i < length; i++) dst[off + i] = getRaw(index + i);
    }

    void putBytes(int index, byte[] src, int off, int length) {
        for (int i = 0; i < length; i++) putRaw(index + i, src[off + i]);
    }

    public ByteBuffer put(byte[] src, int off, int length) {
        checkWritable();
        checkBounds(off, length, src.length);
        if (length > remaining()) throw new BufferOverflowException();
        putBytes(position, src, off, length);
        position += length;
        return this;
    }

    public final ByteBuffer put(byte[] src) {
        return put(src, 0, src.length);
    }

    public ByteBuffer put(ByteBuffer src) {
        if (src == this) throw new IllegalArgumentException("source buffer is this buffer");
        checkWritable();
        int n = src.remaining();
        if (n > remaining()) throw new BufferOverflowException();
        byte[] tmp = new byte[n];
        src.get(tmp, 0, n);
        put(tmp, 0, n);
        return this;
    }

    final void checkWritable() {
        if (isReadOnly) throw new ReadOnlyBufferException();
    }

    public final boolean hasArray() {
        return hb != null && !isReadOnly;
    }

    public final byte[] array() {
        if (hb == null) throw new UnsupportedOperationException();
        if (isReadOnly) throw new ReadOnlyBufferException();
        return hb;
    }

    public final int arrayOffset() {
        if (hb == null) throw new UnsupportedOperationException();
        if (isReadOnly) throw new ReadOnlyBufferException();
        return offset;
    }

    public boolean isReadOnly() {
        return isReadOnly;
    }

    public final ByteOrder order() {
        return bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN;
    }

    public final ByteBuffer order(ByteOrder bo) {
        bigEndian = bo == ByteOrder.BIG_ENDIAN;
        return this;
    }

    // ByteBuffer overrides these Buffer methods with covariant returns.
    public ByteBuffer position(int newPosition) {
        super.position(newPosition);
        return this;
    }

    public ByteBuffer limit(int newLimit) {
        super.limit(newLimit);
        return this;
    }

    public ByteBuffer mark() {
        super.mark();
        return this;
    }

    public ByteBuffer reset() {
        super.reset();
        return this;
    }

    public ByteBuffer clear() {
        super.clear();
        return this;
    }

    public ByteBuffer flip() {
        super.flip();
        return this;
    }

    public ByteBuffer rewind() {
        super.rewind();
        return this;
    }

    // --- multi-byte values ----------------------------------------------------------

    private long getN(int index, int n) {
        long v = 0;
        if (bigEndian) {
            for (int i = 0; i < n; i++) v = (v << 8) | (getRaw(index + i) & 0xffL);
        } else {
            for (int i = n - 1; i >= 0; i--) v = (v << 8) | (getRaw(index + i) & 0xffL);
        }
        return v;
    }

    private void putN(int index, int n, long v) {
        checkWritable();
        if (bigEndian) {
            for (int i = n - 1; i >= 0; i--) {
                putRaw(index + i, (byte) v);
                v >>= 8;
            }
        } else {
            for (int i = 0; i < n; i++) {
                putRaw(index + i, (byte) v);
                v >>= 8;
            }
        }
    }

    public char getChar() {
        return (char) getN(nextGetIndex(2), 2);
    }

    public char getChar(int index) {
        return (char) getN(checkIndex(index, 2), 2);
    }

    public ByteBuffer putChar(char value) {
        putN(nextPutIndex(2), 2, value);
        return this;
    }

    public ByteBuffer putChar(int index, char value) {
        putN(checkIndex(index, 2), 2, value);
        return this;
    }

    public short getShort() {
        return (short) getN(nextGetIndex(2), 2);
    }

    public short getShort(int index) {
        return (short) getN(checkIndex(index, 2), 2);
    }

    public ByteBuffer putShort(short value) {
        putN(nextPutIndex(2), 2, value);
        return this;
    }

    public ByteBuffer putShort(int index, short value) {
        putN(checkIndex(index, 2), 2, value);
        return this;
    }

    public int getInt() {
        return (int) getN(nextGetIndex(4), 4);
    }

    public int getInt(int index) {
        return (int) getN(checkIndex(index, 4), 4);
    }

    public ByteBuffer putInt(int value) {
        putN(nextPutIndex(4), 4, value);
        return this;
    }

    public ByteBuffer putInt(int index, int value) {
        putN(checkIndex(index, 4), 4, value);
        return this;
    }

    public long getLong() {
        return getN(nextGetIndex(8), 8);
    }

    public long getLong(int index) {
        return getN(checkIndex(index, 8), 8);
    }

    public ByteBuffer putLong(long value) {
        putN(nextPutIndex(8), 8, value);
        return this;
    }

    public ByteBuffer putLong(int index, long value) {
        putN(checkIndex(index, 8), 8, value);
        return this;
    }

    public float getFloat() {
        return Float.intBitsToFloat(getInt());
    }

    public float getFloat(int index) {
        return Float.intBitsToFloat(getInt(index));
    }

    public ByteBuffer putFloat(float value) {
        return putInt(Float.floatToRawIntBits(value));
    }

    public ByteBuffer putFloat(int index, float value) {
        return putInt(index, Float.floatToRawIntBits(value));
    }

    public double getDouble() {
        return Double.longBitsToDouble(getLong());
    }

    public double getDouble(int index) {
        return Double.longBitsToDouble(getLong(index));
    }

    public ByteBuffer putDouble(double value) {
        return putLong(Double.doubleToRawLongBits(value));
    }

    public ByteBuffer putDouble(int index, double value) {
        return putLong(index, Double.doubleToRawLongBits(value));
    }

    public int compareTo(ByteBuffer that) {
        int n = Math.min(remaining(), that.remaining());
        for (int i = 0; i < n; i++) {
            int c = Byte.compare(get(position + i), that.get(that.position + i));
            if (c != 0) return c;
        }
        return remaining() - that.remaining();
    }

    public boolean equals(Object ob) {
        if (this == ob) return true;
        if (!(ob instanceof ByteBuffer)) return false;
        return compareTo((ByteBuffer) ob) == 0;
    }

    public int hashCode() {
        int h = 1;
        for (int i = limit - 1; i >= position; i--) h = 31 * h + get(i);
        return h;
    }

    public String toString() {
        return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]";
    }
}
