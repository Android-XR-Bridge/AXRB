package java.io;

public class DataInputStream extends FilterInputStream implements DataInput {
    public DataInputStream(InputStream in) {
        super(in);
    }

    public final int read(byte[] b) throws IOException {
        return in.read(b, 0, b.length);
    }

    public final int read(byte[] b, int off, int len) throws IOException {
        return in.read(b, off, len);
    }

    public final void readFully(byte[] b) throws IOException {
        readFully(b, 0, b.length);
    }

    public final void readFully(byte[] b, int off, int len) throws IOException {
        if (len < 0) throw new IndexOutOfBoundsException();
        int n = 0;
        while (n < len) {
            int count = in.read(b, off + n, len - n);
            if (count < 0) throw new EOFException();
            n += count;
        }
    }

    public final int skipBytes(int n) throws IOException {
        int total = 0, cur;
        while (total < n && (cur = (int) in.skip(n - total)) > 0) total += cur;
        return total;
    }

    private int next() throws IOException {
        int ch = in.read();
        if (ch < 0) throw new EOFException();
        return ch;
    }

    public final boolean readBoolean() throws IOException {
        return next() != 0;
    }

    public final byte readByte() throws IOException {
        return (byte) next();
    }

    public final int readUnsignedByte() throws IOException {
        return next();
    }

    public final short readShort() throws IOException {
        int hi = next();
        return (short) ((hi << 8) | next());
    }

    public final int readUnsignedShort() throws IOException {
        int hi = next();
        return (hi << 8) | next();
    }

    public final char readChar() throws IOException {
        return (char) readUnsignedShort();
    }

    public final int readInt() throws IOException {
        int a = next(), b = next(), c = next();
        return (a << 24) | (b << 16) | (c << 8) | next();
    }

    public final long readLong() throws IOException {
        long hi = readInt();
        return (hi << 32) | (readInt() & 0xffffffffL);
    }

    public final float readFloat() throws IOException {
        return Float.intBitsToFloat(readInt());
    }

    public final double readDouble() throws IOException {
        return Double.longBitsToDouble(readLong());
    }

    @Deprecated
    public final String readLine() throws IOException {
        StringBuilder sb = new StringBuilder();
        int c;
        while ((c = in.read()) >= 0 && c != '\n') {
            if (c != '\r') sb.append((char) c);
        }
        return c < 0 && sb.length() == 0 ? null : sb.toString();
    }

    public final String readUTF() throws IOException {
        return readUTF(this);
    }

    /** Modified UTF-8, as written by DataOutputStream.writeUTF. */
    public static final String readUTF(DataInput in) throws IOException {
        int utflen = in.readUnsignedShort();
        byte[] bytes = new byte[utflen];
        in.readFully(bytes, 0, utflen);
        StringBuilder sb = new StringBuilder(utflen);
        int i = 0;
        while (i < utflen) {
            int c = bytes[i] & 0xff;
            if (c < 0x80) {
                sb.append((char) c);
                i++;
            } else if ((c >> 5) == 6) {
                if (i + 1 >= utflen) throw new UTFDataFormatException("malformed input: partial character at end");
                sb.append((char) (((c & 0x1f) << 6) | (bytes[i + 1] & 0x3f)));
                i += 2;
            } else if ((c >> 4) == 14) {
                if (i + 2 >= utflen) throw new UTFDataFormatException("malformed input: partial character at end");
                sb.append((char) (((c & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f)));
                i += 3;
            } else {
                throw new UTFDataFormatException("malformed input around byte " + i);
            }
        }
        return sb.toString();
    }
}
