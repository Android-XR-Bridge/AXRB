package android.app;

import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.MessageQueue;
import android.util.AttributeSet;
import android.view.InputQueue;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewTreeObserver;
import java.io.File;

/**
 * An activity implemented in native code (frameworks/base NativeActivity).
 * The natives live in libandroid.so (direct/guest/proxy/native_activity.c);
 * there is no input queue, as VR games read input through OpenXR.
 */
public class NativeActivity extends Activity
        implements SurfaceHolder.Callback2, InputQueue.Callback, ViewTreeObserver.OnGlobalLayoutListener {
    public static final String META_DATA_LIB_NAME = "android.app.lib_name";
    public static final String META_DATA_FUNC_NAME = "android.app.func_name";
    private static final String KEY_NATIVE_SAVED_STATE = "android:native_state";

    private NativeContentView mNativeContentView;
    private long mNativeHandle;
    private SurfaceHolder mCurSurfaceHolder;
    final int[] mLocation = new int[2];
    int mLastContentX, mLastContentY, mLastContentWidth, mLastContentHeight;
    private boolean mDestroyed;

    static {
        System.loadLibrary("android");
    }

    private native long loadNativeCode(String path, String funcname, MessageQueue queue, String internalDataPath,
            String obbPath, String externalDataPath, int sdkVersion, android.content.res.AssetManager assetMgr,
            byte[] savedState, ClassLoader classLoader, String libraryPath);

    private native String getDlError();

    private native void unloadNativeCode(long handle);

    private native void onStartNative(long handle);

    private native void onResumeNative(long handle);

    private native byte[] onSaveInstanceStateNative(long handle);

    private native void onPauseNative(long handle);

    private native void onStopNative(long handle);

    private native void onConfigurationChangedNative(long handle);

    private native void onLowMemoryNative(long handle);

    private native void onWindowFocusChangedNative(long handle, boolean focused);

    private native void onSurfaceCreatedNative(long handle, android.view.Surface surface);

    private native void onSurfaceChangedNative(long handle, android.view.Surface surface, int format, int width,
            int height);

    private native void onSurfaceRedrawNeededNative(long handle, android.view.Surface surface);

    private native void onSurfaceDestroyedNative(long handle);

    private native void onContentRectChangedNative(long handle, int x, int y, int w, int h);

    static class NativeContentView extends View {
        NativeActivity mActivity;

        public NativeContentView(android.content.Context context) {
            super(context);
        }

        public NativeContentView(android.content.Context context, AttributeSet attrs) {
            super(context, attrs);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String libname = "main";
        String funcname = "ANativeActivity_onCreate";
        getWindow().takeSurface(this);
        getWindow().takeInputQueue(this);
        getWindow().setFormat(PixelFormat.RGB_565);

        mNativeContentView = new NativeContentView(this);
        mNativeContentView.mActivity = this;
        setContentView(mNativeContentView);
        mNativeContentView.requestFocus();
        mNativeContentView.getViewTreeObserver().addOnGlobalLayoutListener(this);

        try {
            ActivityInfo ai = getPackageManager().getActivityInfo(getIntent().getComponent(),
                    PackageManager.GET_META_DATA);
            if (ai.metaData != null) {
                String ln = ai.metaData.getString(META_DATA_LIB_NAME);
                if (ln != null) libname = ln;
                String fn = ai.metaData.getString(META_DATA_FUNC_NAME);
                if (fn != null) funcname = fn;
            }
        } catch (PackageManager.NameNotFoundException e) {
            throw new RuntimeException("Error getting activity info", e);
        }

        // Loading through the class loader first runs JNI_OnLoad and finds the
        // library in the app's native directory; dlopen by soname then returns it.
        System.loadLibrary(libname);
        String path = "lib" + libname + ".so";
        byte[] nativeSavedState = savedInstanceState != null
                ? savedInstanceState.getByteArray(KEY_NATIVE_SAVED_STATE) : null;
        mNativeHandle = loadNativeCode(path, funcname, Looper.myQueue(), getAbsolutePath(getFilesDir()),
                getAbsolutePath(getObbDir()), getAbsolutePath(getExternalFilesDir(null)), Build.VERSION.SDK_INT,
                getAssets(), nativeSavedState, getClassLoader(), path);
        if (mNativeHandle == 0) {
            throw new UnsatisfiedLinkError("Unable to load native library \"" + path + "\": " + getDlError());
        }
        super.onCreate(savedInstanceState);
    }

    private static String getAbsolutePath(File file) {
        return file != null ? file.getAbsolutePath() : null;
    }

    @Override
    protected void onDestroy() {
        mDestroyed = true;
        if (mCurSurfaceHolder != null) {
            onSurfaceDestroyedNative(mNativeHandle);
            mCurSurfaceHolder = null;
        }
        unloadNativeCode(mNativeHandle);
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        super.onPause();
        onPauseNative(mNativeHandle);
    }

    @Override
    protected void onResume() {
        super.onResume();
        onResumeNative(mNativeHandle);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        byte[] state = onSaveInstanceStateNative(mNativeHandle);
        if (state != null) outState.putByteArray(KEY_NATIVE_SAVED_STATE, state);
    }

    @Override
    protected void onStart() {
        super.onStart();
        onStartNative(mNativeHandle);
    }

    @Override
    protected void onStop() {
        super.onStop();
        onStopNative(mNativeHandle);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (!mDestroyed) onConfigurationChangedNative(mNativeHandle);
    }

    @Override
    public void onLowMemory() {
        super.onLowMemory();
        if (!mDestroyed) onLowMemoryNative(mNativeHandle);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!mDestroyed) onWindowFocusChangedNative(mNativeHandle, hasFocus);
    }

    public void surfaceCreated(SurfaceHolder holder) {
        if (!mDestroyed) {
            mCurSurfaceHolder = holder;
            onSurfaceCreatedNative(mNativeHandle, holder.getSurface());
        }
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (!mDestroyed) {
            mCurSurfaceHolder = holder;
            onSurfaceChangedNative(mNativeHandle, holder.getSurface(), format, width, height);
        }
    }

    public void surfaceRedrawNeeded(SurfaceHolder holder) {
        if (!mDestroyed) {
            mCurSurfaceHolder = holder;
            onSurfaceRedrawNeededNative(mNativeHandle, holder.getSurface());
        }
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        mCurSurfaceHolder = null;
        if (!mDestroyed) onSurfaceDestroyedNative(mNativeHandle);
    }

    public void onInputQueueCreated(InputQueue queue) {}

    public void onInputQueueDestroyed(InputQueue queue) {}

    public void onGlobalLayout() {
        mNativeContentView.getLocationInWindow(mLocation);
        int w = mNativeContentView.getWidth();
        int h = mNativeContentView.getHeight();
        if (mLocation[0] != mLastContentX || mLocation[1] != mLastContentY || w != mLastContentWidth
                || h != mLastContentHeight) {
            mLastContentX = mLocation[0];
            mLastContentY = mLocation[1];
            mLastContentWidth = w;
            mLastContentHeight = h;
            if (!mDestroyed) onContentRectChangedNative(mNativeHandle, mLastContentX, mLastContentY,
                    mLastContentWidth, mLastContentHeight);
        }
    }

    void setWindowFlags(int flags, int mask) {
        getWindow().setFlags(flags, mask);
    }

    void setWindowFormat(int format) {
        getWindow().setFormat(format);
    }

    void showIme(int mode) {}

    void hideIme(int mode) {}
}
