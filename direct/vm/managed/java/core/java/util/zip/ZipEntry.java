package java.util.zip;

public class ZipEntry implements Cloneable {
    public static final int STORED = 0;
    public static final int DEFLATED = 8;

    String name;
    long time = -1;
    long crc = -1;
    long size = -1;
    long csize = -1;
    int method = -1;
    long dataOffset;
    String comment;
    byte[] extra;

    public ZipEntry(String name) {
        if (name == null) throw new NullPointerException("name");
        this.name = name;
    }

    public ZipEntry(ZipEntry e) {
        name = e.name;
        time = e.time;
        crc = e.crc;
        size = e.size;
        csize = e.csize;
        method = e.method;
        dataOffset = e.dataOffset;
        comment = e.comment;
        extra = e.extra;
    }

    public String getName() {
        return name;
    }

    public long getTime() {
        return time;
    }

    public void setTime(long time) {
        this.time = time;
    }

    public long getSize() {
        return size;
    }

    public void setSize(long size) {
        this.size = size;
    }

    public long getCompressedSize() {
        return csize;
    }

    public void setCompressedSize(long csize) {
        this.csize = csize;
    }

    public long getCrc() {
        return crc;
    }

    public void setCrc(long crc) {
        this.crc = crc;
    }

    public int getMethod() {
        return method;
    }

    public void setMethod(int method) {
        this.method = method;
    }

    public byte[] getExtra() {
        return extra;
    }

    public void setExtra(byte[] extra) {
        this.extra = extra;
    }

    public String getComment() {
        return comment;
    }

    public void setComment(String comment) {
        this.comment = comment;
    }

    /** Hidden on Android: where the entry's data starts in the archive. */
    public long getDataOffset() {
        return dataOffset;
    }

    public boolean isDirectory() {
        return name.endsWith("/");
    }

    public String toString() {
        return name;
    }

    public int hashCode() {
        return name.hashCode();
    }

    public Object clone() {
        return new ZipEntry(this);
    }
}
