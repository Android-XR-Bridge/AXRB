package com.axrb.runtime;

import android.graphics.*;
import android.view.Surface;
import java.nio.ByteBuffer;
import java.util.concurrent.locks.ReentrantLock;

/** AOSP software Canvas producer for OpenXR Android-surface overlay layers. */
public final class HostSurface extends Surface {
    private final SurfaceTexture texture;
    private final Bitmap bitmap;
    private final ReentrantLock pixels = new ReentrantLock();
    private Canvas canvas;
    private long serial = 1;
    private boolean released;
    private static final java.util.concurrent.atomic.AtomicLong nextMediaHandle = new java.util.concurrent.atomic.AtomicLong(1);
    final long mediaHandle = nextMediaHandle.getAndIncrement();
    private static native ByteBuffer bufferAt(long address, int bytes);

    public HostSurface(int width, int height) { this(new SurfaceTexture(false), width, height); }
    private HostSurface(SurfaceTexture texture, int width, int height) {
        super(texture);
        this.texture = texture;
        texture.setDefaultBufferSize(width, height);
        bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
    }
    @Override public Canvas lockCanvas(Rect dirty) {
        pixels.lock();
        if (released || canvas != null) { pixels.unlock(); throw new IllegalStateException("Surface unavailable"); }
        canvas = new Canvas(bitmap);
        return canvas;
    }
    @Override public Canvas lockHardwareCanvas() { return lockCanvas(null); }
    @Override public void unlockCanvasAndPost(Canvas drawing) {
        if (!pixels.isHeldByCurrentThread() || drawing != canvas) throw new IllegalArgumentException("Canvas was not locked here");
        canvas = null;
        ++serial;
        pixels.unlock();
    }
    public long readFrame(long address, int width, int height, long previous) {
        pixels.lock();
        try {
            long mediaSerial = HostMedia.readFrame(mediaHandle, address, width, height, previous & ~(1L << 60));
            if (mediaSerial != 0) return mediaSerial | (1L << 60);
            if (released || serial == previous) return previous;
            Bitmap frame = width == bitmap.getWidth() && height == bitmap.getHeight() ? bitmap : Bitmap.createScaledBitmap(bitmap, width, height, true);
            try { frame.copyPixelsToBuffer(bufferAt(address, Math.multiplyExact(Math.multiplyExact(width, height), 4))); }
            finally { if (frame != bitmap) frame.recycle(); }
            return serial;
        } finally { pixels.unlock(); }
    }
    @Override public void release() {
        pixels.lock();
        try {
            if (!released) { released = true; HostMedia.releaseSurface(mediaHandle); bitmap.recycle(); texture.release(); super.release(); }
        } finally { pixels.unlock(); }
    }
}
