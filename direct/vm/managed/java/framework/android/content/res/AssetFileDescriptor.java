package android.content.res;

import android.os.ParcelFileDescriptor;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.IOException;

public class AssetFileDescriptor implements java.io.Closeable {
    public static final long UNKNOWN_LENGTH = -1;

    private final ParcelFileDescriptor mFd;
    private final long mStartOffset;
    private final long mLength;

    public AssetFileDescriptor(ParcelFileDescriptor fd, long startOffset, long length) {
        mFd = fd;
        mStartOffset = startOffset;
        mLength = length;
    }

    public ParcelFileDescriptor getParcelFileDescriptor() {
        return mFd;
    }

    public FileDescriptor getFileDescriptor() {
        return mFd.getFileDescriptor();
    }

    public long getStartOffset() {
        return mStartOffset;
    }

    public long getLength() {
        return mLength >= 0 ? mLength : mFd.getStatSize();
    }

    public long getDeclaredLength() {
        return mLength;
    }

    public void close() throws IOException {
        mFd.close();
    }

    public FileInputStream createInputStream() throws IOException {
        FileInputStream in = new FileInputStream(mFd.getFileDescriptor());
        in.skip(mStartOffset);
        return in;
    }

    public String toString() {
        return "{AssetFileDescriptor: " + mFd + " start=" + mStartOffset + " len=" + mLength + "}";
    }
}
