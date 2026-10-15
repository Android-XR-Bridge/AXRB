package java.nio;

/**
 * A byte buffer over guest memory. Created by allocateDirect (fresh guest
 * memory) or by JNI NewDirectByteBuffer(address, capacity). The memory of an
 * allocateDirect buffer is never freed (the runtime runs no finalizers).
 */
public class DirectByteBuffer extends ByteBuffer {
    DirectByteBuffer(int capacity) {
        super(-1, 0, capacity, capacity, null, 0);
        address = capacity > 0 ? nativeAlloc(capacity) : 0;
        if (capacity > 0 && address == 0) throw new OutOfMemoryError("cannot allocate a direct buffer of " + capacity);
    }

    /** JNI NewDirectByteBuffer. */
    DirectByteBuffer(long address, int capacity) {
        super(-1, 0, capacity, capacity, null, 0);
        this.address = address;
    }

    private DirectByteBuffer(long address, int mark, int pos, int lim, int cap) {
        super(mark, pos, lim, cap, null, 0);
        this.address = address;
    }

    public final long address() {
        return address;
    }

    byte getRaw(int index) {
        return peek(address + index);
    }

    void putRaw(int index, byte b) {
        poke(address + index, b);
    }

    void getBytes(int index, byte[] dst, int off, int length) {
        peekBytes(address + index, dst, off, length);
    }

    void putBytes(int index, byte[] src, int off, int length) {
        pokeBytes(address + index, src, off, length);
    }

    public boolean isDirect() {
        return true;
    }

    public ByteBuffer slice() {
        int rem = remaining();
        DirectByteBuffer b = new DirectByteBuffer(address + position, -1, 0, rem, rem);
        b.isReadOnly = isReadOnly;
        return b;
    }

    public ByteBuffer duplicate() {
        DirectByteBuffer b = new DirectByteBuffer(address, mark, position, limit, capacity);
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
        byte[] tmp = new byte[rem];
        peekBytes(address + position, tmp, 0, rem);
        pokeBytes(address, tmp, 0, rem);
        position(rem);
        limit(capacity);
        mark = -1;
        return this;
    }

    private static native long nativeAlloc(int capacity);

    private static native byte peek(long address);

    private static native void poke(long address, byte value);

    private static native void peekBytes(long address, byte[] dst, int off, int length);

    private static native void pokeBytes(long address, byte[] src, int off, int length);
}
