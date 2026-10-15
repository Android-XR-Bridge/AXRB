package android.os;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileNotFoundException;
import java.io.IOException;

public class ParcelFileDescriptor implements java.io.Closeable {
    public static final int MODE_READ_ONLY = 0x10000000;
    public static final int MODE_WRITE_ONLY = 0x20000000;
    public static final int MODE_READ_WRITE = 0x30000000;
    public static final int MODE_CREATE = 0x08000000;
    public static final int MODE_TRUNCATE = 0x04000000;
    public static final int MODE_APPEND = 0x02000000;

    private final FileDescriptor mFd;
    private boolean mClosed;

    public ParcelFileDescriptor(FileDescriptor fd) {
        mFd = fd;
    }

    public static ParcelFileDescriptor adoptFd(int fd) {
        FileDescriptor d = new FileDescriptor();
        d.setInt$(fd);
        return new ParcelFileDescriptor(d);
    }

    public static ParcelFileDescriptor open(File file, int mode) throws FileNotFoundException {
        int fd = nativeOpen(file.getPath(), mode);
        if (fd < 0) throw new FileNotFoundException(file.getPath() + ": open failed (errno " + -fd + ")");
        return adoptFd(fd);
    }

    public FileDescriptor getFileDescriptor() {
        return mFd;
    }

    public int getFd() {
        if (mClosed) throw new IllegalStateException("Already closed");
        return mFd.getInt$();
    }

    public int detachFd() {
        int fd = getFd();
        mFd.setInt$(-1);
        mClosed = true;
        return fd;
    }

    public long getStatSize() {
        return nativeStatSize(mFd.getInt$());
    }

    public void close() throws IOException {
        if (mClosed) return;
        mClosed = true;
        nativeClose(mFd.getInt$());
        mFd.setInt$(-1);
    }

    public String toString() {
        return "{ParcelFileDescriptor: " + mFd + "}";
    }

    static native int nativeOpen(String path, int mode);

    static native long nativeStatSize(int fd);

    static native void nativeClose(int fd);
}
