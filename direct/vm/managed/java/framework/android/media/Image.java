package android.media;

import java.nio.ByteBuffer;

public abstract class Image implements AutoCloseable {
    public abstract static class Plane {
        protected Plane() {}

        public abstract int getRowStride();

        public abstract int getPixelStride();

        public abstract ByteBuffer getBuffer();
    }

    protected Image() {}

    public abstract int getFormat();

    public abstract int getWidth();

    public abstract int getHeight();

    public abstract long getTimestamp();

    public abstract Plane[] getPlanes();

    public abstract void close();
}
