package java.io;

public class DataOutputStream extends FilterOutputStream implements DataOutput {
    protected int written;

    public DataOutputStream(OutputStream out) {
        super(out);
    }

    private void incCount(int value) {
        int temp = written + value;
        written = temp < 0 ? Integer.MAX_VALUE : temp;
    }

    public synchronized void write(int b) throws IOException {
        out.write(b);
        incCount(1);
    }

    public synchronized void write(byte[] b, int off, int len) throws IOException {
        out.write(b, off, len);
        incCount(len);
    }

    public void flush() throws IOException {
        out.flush();
    }

    public final void writeBoolean(boolean v) throws IOException {
        write(v ? 1 : 0);
    }

    public final void writeByte(int v) throws IOException {
        write(v);
    }

    public final void writeShort(int v) throws IOException {
        write(new byte[] {(byte) (v >>> 8), (byte) v}, 0, 2);
    }

    public final void writeChar(int v) throws IOException {
        writeShort(v);
    }

    public final void writeInt(int v) throws IOException {
        write(new byte[] {(byte) (v >>> 24), (byte) (v >>> 16), (byte) (v >>> 8), (byte) v}, 0, 4);
    }

    public final void writeLong(long v) throws IOException {
        writeInt((int) (v >>> 32));
        writeInt((int) v);
    }

    public final void writeFloat(float v) throws IOException {
        writeInt(Float.floatToIntBits(v));
    }

    public final void writeDouble(double v) throws IOException {
        writeLong(Double.doubleToLongBits(v));
    }

    public final void writeBytes(String s) throws IOException {
        for (int i = 0; i < s.length(); i++) write((byte) s.charAt(i));
    }

    public final void writeChars(String s) throws IOException {
        for (int i = 0; i < s.length(); i++) writeChar(s.charAt(i));
    }

    /** Modified UTF-8 with a two-byte length prefix. */
    public final void writeUTF(String str) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(str.length() + 2);
        for (int i = 0; i < str.length(); i++) {
            char c = str.charAt(i);
            if (c >= 0x0001 && c <= 0x007f) {
                bytes.write(c);
            } else if (c > 0x07ff) {
                bytes.write(0xe0 | ((c >> 12) & 0x0f));
                bytes.write(0x80 | ((c >> 6) & 0x3f));
                bytes.write(0x80 | (c & 0x3f));
            } else {
                bytes.write(0xc0 | ((c >> 6) & 0x1f));
                bytes.write(0x80 | (c & 0x3f));
            }
        }
        if (bytes.size() > 65535) throw new UTFDataFormatException("encoded string too long: " + bytes.size() + " bytes");
        writeShort(bytes.size());
        write(bytes.toByteArray(), 0, bytes.size());
    }

    public final int size() {
        return written;
    }
}
