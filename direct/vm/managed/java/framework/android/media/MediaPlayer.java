package android.media;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.view.Surface;
import android.view.SurfaceHolder;
import java.io.FileDescriptor;
import java.io.IOException;

/**
 * Video playback decoded on the host (Media Foundation). Frames go to the
 * Surface given by setSurface; audio tracks are not played.
 */
public class MediaPlayer {
    public static final int MEDIA_ERROR_UNKNOWN = 1;
    public static final int MEDIA_ERROR_UNSUPPORTED = -1010;

    public interface OnPreparedListener {
        void onPrepared(MediaPlayer mp);
    }

    public interface OnCompletionListener {
        void onCompletion(MediaPlayer mp);
    }

    public interface OnBufferingUpdateListener {
        void onBufferingUpdate(MediaPlayer mp, int percent);
    }

    public interface OnVideoSizeChangedListener {
        void onVideoSizeChanged(MediaPlayer mp, int width, int height);
    }

    public interface OnErrorListener {
        boolean onError(MediaPlayer mp, int what, int extra);
    }

    public interface OnInfoListener {
        boolean onInfo(MediaPlayer mp, int what, int extra);
    }

    public interface OnSeekCompleteListener {
        void onSeekComplete(MediaPlayer mp);
    }

    private long mNative;
    private boolean mHasSource;
    private int mWidth;
    private int mHeight;
    private int mDuration;
    private boolean mLooping;
    private OnErrorListener mOnError;
    private OnCompletionListener mOnCompletion;
    private OnPreparedListener mOnPrepared;
    private OnVideoSizeChangedListener mOnVideoSize;
    private OnSeekCompleteListener mOnSeekComplete;

    public MediaPlayer() {
        mNative = nativeCreate();
    }

    public void setDataSource(Context context, Uri uri) throws IOException {
        String path = uri.getPath();
        if (path == null) throw new IOException("setDataSource failed: unsupported uri " + uri);
        setDataSource(path);
    }

    public void setDataSource(String path) throws IOException {
        if (path.startsWith("file://")) path = path.substring(7);
        ParcelFileDescriptor fd = ParcelFileDescriptor.open(new java.io.File(path), ParcelFileDescriptor.MODE_READ_ONLY);
        try {
            setDataSource(fd.getFileDescriptor(), 0, -1);
        } finally {
            fd.close();
        }
    }

    public void setDataSource(AssetFileDescriptor afd) throws IOException {
        setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getDeclaredLength());
    }

    public void setDataSource(FileDescriptor fd) throws IOException {
        setDataSource(fd, 0, -1);
    }

    public void setDataSource(FileDescriptor fd, long offset, long length) throws IOException {
        if (fd == null || !fd.valid()) throw new IllegalArgumentException("invalid file descriptor");
        if (!nativeSetSource(mNative, fd.getInt$(), offset, length)) {
            throw new IOException("setDataSource failed.: status=0x80000000");
        }
        mHasSource = true;
    }

    public void setDisplay(SurfaceHolder sh) {
        setSurface(sh != null ? sh.getSurface() : null);
    }

    public void setSurface(Surface surface) {
        nativeSetSurface(mNative, surface != null ? surface.getNativeHandle() : 0);
    }

    public void prepare() throws IOException {
        if (!mHasSource) throw new IllegalStateException("prepare called in state 1");
        int[] info = nativePrepare(mNative);
        if (info == null) throw new IOException("Prepare failed.: status=0x1");
        mWidth = info[0];
        mHeight = info[1];
        mDuration = info[2];
        nativeSetLooping(mNative, mLooping);
        if (mOnVideoSize != null) mOnVideoSize.onVideoSizeChanged(this, mWidth, mHeight);
    }

    public void prepareAsync() {
        final Handler handler = new Handler(Looper.getMainLooper());
        new Thread(new Runnable() {
            public void run() {
                boolean ok;
                try {
                    prepare();
                    ok = true;
                } catch (Exception e) {
                    ok = false;
                }
                final boolean prepared = ok;
                handler.post(new Runnable() {
                    public void run() {
                        if (prepared) {
                            if (mOnPrepared != null) mOnPrepared.onPrepared(MediaPlayer.this);
                        } else {
                            boolean handled = mOnError != null
                                    && mOnError.onError(MediaPlayer.this, MEDIA_ERROR_UNKNOWN, MEDIA_ERROR_UNSUPPORTED);
                            if (!handled && mOnCompletion != null) mOnCompletion.onCompletion(MediaPlayer.this);
                        }
                    }
                });
            }
        }, "MediaPlayer-prepare").start();
    }

    public void start() {
        nativeStart(mNative);
    }

    public void stop() {
        nativeStop(mNative);
    }

    public void pause() {
        nativePause(mNative);
    }

    public void reset() {
        nativeStop(mNative);
        mHasSource = false;
        mWidth = mHeight = mDuration = 0;
    }

    public void release() {
        if (mNative != 0) nativeRelease(mNative);
        mNative = 0;
    }

    public void seekTo(int msec) {
        nativeSeek(mNative, msec);
        if (mOnSeekComplete != null) mOnSeekComplete.onSeekComplete(this);
    }

    public boolean isPlaying() {
        return mNative != 0 && nativeIsPlaying(mNative);
    }

    public int getCurrentPosition() {
        return mNative != 0 ? nativePosition(mNative) : 0;
    }

    public int getDuration() {
        return mDuration;
    }

    public int getVideoWidth() {
        return mWidth;
    }

    public int getVideoHeight() {
        return mHeight;
    }

    public int getAudioSessionId() {
        return 1;
    }

    public void setAudioStreamType(int streamtype) {}

    public void setLooping(boolean looping) {
        mLooping = looping;
        if (mNative != 0) nativeSetLooping(mNative, looping);
    }

    public boolean isLooping() {
        return mLooping;
    }

    public void setVolume(float leftVolume, float rightVolume) {}

    public void setScreenOnWhilePlaying(boolean screenOn) {}

    public void setOnPreparedListener(OnPreparedListener listener) {
        mOnPrepared = listener;
    }

    public void setOnCompletionListener(OnCompletionListener listener) {
        mOnCompletion = listener;
    }

    public void setOnBufferingUpdateListener(OnBufferingUpdateListener listener) {}

    public void setOnVideoSizeChangedListener(OnVideoSizeChangedListener listener) {
        mOnVideoSize = listener;
    }

    public void setOnErrorListener(OnErrorListener listener) {
        mOnError = listener;
    }

    public void setOnInfoListener(OnInfoListener listener) {}

    public void setOnSeekCompleteListener(OnSeekCompleteListener listener) {
        mOnSeekComplete = listener;
    }

    private static native long nativeCreate();

    private static native boolean nativeSetSource(long player, int fd, long offset, long length);

    private static native void nativeSetSurface(long player, long surface);

    private static native int[] nativePrepare(long player);

    private static native void nativeStart(long player);

    private static native void nativePause(long player);

    private static native void nativeStop(long player);

    private static native void nativeSeek(long player, int msec);

    private static native void nativeSetLooping(long player, boolean looping);

    private static native boolean nativeIsPlaying(long player);

    private static native int nativePosition(long player);

    private static native void nativeRelease(long player);
}
