package android.os;

/** File system capacity of a guest path. */
public class StatFs {
    // {blockSize, blockCount, freeBlocks, availableBlocks}
    private long[] mStat;

    public StatFs(String path) {
        restat(path);
    }

    public void restat(String path) {
        long[] s = nativeStatFs(path);
        if (s == null) throw new IllegalArgumentException("Invalid path: " + path);
        mStat = s;
    }

    public int getBlockSize() {
        return (int) mStat[0];
    }

    public long getBlockSizeLong() {
        return mStat[0];
    }

    public int getBlockCount() {
        return (int) mStat[1];
    }

    public long getBlockCountLong() {
        return mStat[1];
    }

    public int getFreeBlocks() {
        return (int) mStat[2];
    }

    public long getFreeBlocksLong() {
        return mStat[2];
    }

    public int getAvailableBlocks() {
        return (int) mStat[3];
    }

    public long getAvailableBlocksLong() {
        return mStat[3];
    }

    public long getFreeBytes() {
        return mStat[2] * mStat[0];
    }

    public long getAvailableBytes() {
        return mStat[3] * mStat[0];
    }

    public long getTotalBytes() {
        return mStat[1] * mStat[0];
    }

    private static native long[] nativeStatFs(String path);
}
