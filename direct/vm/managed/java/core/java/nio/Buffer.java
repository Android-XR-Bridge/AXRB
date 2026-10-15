package java.nio;

public abstract class Buffer {
    int mark = -1;
    int position;
    int limit;
    int capacity;
    /** Guest address of a direct buffer's first element; 0 for heap buffers. */
    long address;

    Buffer(int mark, int pos, int lim, int cap) {
        if (cap < 0) throw new IllegalArgumentException("capacity < 0: " + cap);
        this.capacity = cap;
        limit(lim);
        position(pos);
        if (mark >= 0) {
            if (mark > pos) throw new IllegalArgumentException();
            this.mark = mark;
        }
    }

    public final int capacity() {
        return capacity;
    }

    public final int position() {
        return position;
    }

    public Buffer position(int newPosition) {
        if (newPosition > limit || newPosition < 0) throw new IllegalArgumentException("bad position " + newPosition);
        if (mark > newPosition) mark = -1;
        position = newPosition;
        return this;
    }

    public final int limit() {
        return limit;
    }

    public Buffer limit(int newLimit) {
        if (newLimit > capacity || newLimit < 0) throw new IllegalArgumentException("bad limit " + newLimit);
        limit = newLimit;
        if (position > newLimit) position = newLimit;
        if (mark > newLimit) mark = -1;
        return this;
    }

    public Buffer mark() {
        mark = position;
        return this;
    }

    public Buffer reset() {
        if (mark < 0) throw new InvalidMarkException();
        position = mark;
        return this;
    }

    public Buffer clear() {
        position = 0;
        limit = capacity;
        mark = -1;
        return this;
    }

    public Buffer flip() {
        limit = position;
        position = 0;
        mark = -1;
        return this;
    }

    public Buffer rewind() {
        position = 0;
        mark = -1;
        return this;
    }

    public final int remaining() {
        int rem = limit - position;
        return rem > 0 ? rem : 0;
    }

    public final boolean hasRemaining() {
        return position < limit;
    }

    public abstract boolean isReadOnly();

    public abstract boolean hasArray();

    public abstract Object array();

    public abstract int arrayOffset();

    public abstract boolean isDirect();

    final int nextGetIndex(int n) {
        if (limit - position < n) throw new BufferUnderflowException();
        int p = position;
        position += n;
        return p;
    }

    final int nextPutIndex(int n) {
        if (limit - position < n) throw new BufferOverflowException();
        int p = position;
        position += n;
        return p;
    }

    final int checkIndex(int i, int n) {
        if (i < 0 || n > limit - i) throw new IndexOutOfBoundsException("index " + i + ", limit " + limit);
        return i;
    }

    static void checkBounds(int off, int len, int size) {
        if ((off | len | (off + len) | (size - (off + len))) < 0) throw new IndexOutOfBoundsException();
    }
}
