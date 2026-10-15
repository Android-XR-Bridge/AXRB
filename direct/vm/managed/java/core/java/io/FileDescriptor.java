package java.io;

/** A guest file descriptor number (what native code receives through JNI). */
public final class FileDescriptor {
    public static final FileDescriptor in = new FileDescriptor(0);
    public static final FileDescriptor out = new FileDescriptor(1);
    public static final FileDescriptor err = new FileDescriptor(2);

    private int descriptor;
    private long ownerId;

    public FileDescriptor() {
        descriptor = -1;
    }

    FileDescriptor(int descriptor) {
        this.descriptor = descriptor;
    }

    public boolean valid() {
        return descriptor != -1;
    }

    public void sync() throws SyncFailedException {}

    /** Hidden on Android (libcore): the raw descriptor. */
    public int getInt$() {
        return descriptor;
    }

    public void setInt$(int fd) {
        descriptor = fd;
    }

    public String toString() {
        return "FileDescriptor[" + descriptor + "]";
    }
}
