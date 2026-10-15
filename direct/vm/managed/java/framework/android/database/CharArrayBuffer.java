package android.database;

public final class CharArrayBuffer {
    public char[] data;
    public int sizeCopied;

    public CharArrayBuffer(int size) {
        data = new char[size];
    }

    public CharArrayBuffer(char[] buf) {
        data = buf;
    }
}
