package java.nio;

final class HeapByteBuffer extends ByteBuffer {
    HeapByteBuffer(byte[] hb, int mark, int pos, int lim, int cap, int offset) {
        super(mark, pos, lim, cap, hb, offset);
    }

    byte getRaw(int index) {
        return hb[offset + index];
    }

    void putRaw(int index, byte b) {
        hb[offset + index] = b;
    }

    void getBytes(int index, byte[] dst, int off, int length) {
        System.arraycopy(hb, offset + index, dst, off, length);
    }

    void putBytes(int index, byte[] src, int off, int length) {
        System.arraycopy(src, off, hb, offset + index, length);
    }

    public boolean isDirect() {
        return false;
    }

    public ByteBuffer slice() {
        int rem = remaining();
        HeapByteBuffer b = new HeapByteBuffer(hb, -1, 0, rem, rem, offset + position);
        b.isReadOnly = isReadOnly;
        return b;
    }

    public ByteBuffer duplicate() {
        HeapByteBuffer b = new HeapByteBuffer(hb, mark, position, limit, capacity, offset);
        b.isReadOnly = isReadOnly;
        b.bigEndian = bigEndian;
        return b;
    }

    public ByteBuffer asReadOnlyBuffer() {
        ByteBuffer b = duplicate();
        b.isReadOnly = true;
        return b;
    }

    public ByteBuffer compact() {
        checkWritable();
        int rem = remaining();
        System.arraycopy(hb, offset + position, hb, offset, rem);
        position(rem);
        limit(capacity);
        mark = -1;
        return this;
    }
}
