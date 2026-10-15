package android.app;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.SuperNotCalledException;
import android.view.ContextThemeWrapper;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import com.android.internal.policy.PhoneWindow;

public class Activity extends ContextThemeWrapper
        implements Window.Callback, android.view.KeyEvent.Callback, android.content.ComponentCallbacks2 {
    public static final int RESULT_CANCELED = 0;
    public static final int RESULT_OK = -1;
    public static final int RESULT_FIRST_USER = 1;

    private ActivityThread mThread;
    private Application mApplication;
    private Intent mIntent;
    private ComponentName mComponent;
    private CharSequence mTitle;
    private PhoneWindow mWindow;
    private Thread mUiThread;
    private Handler mHandler;
    private int mRequestedOrientation = -1;
    private int mResultCode = RESULT_CANCELED;
    private Intent mResultData;
    ComponentName mCallingActivity;

    boolean mCalled;
    boolean mResumed;
    boolean mStopped = true;
    boolean mFinished;
    boolean mDestroyed;

    public Activity() {
        super();
    }

    final void attach(Context base, ActivityThread thread, Application application, Intent intent,
            ComponentName component, CharSequence title) {
        attachBaseContext(base);
        mThread = thread;
        mApplication = application;
        mIntent = intent;
        mComponent = component;
        mTitle = title;
        mUiThread = Thread.currentThread();
        mHandler = new Handler(Looper.getMainLooper());
        mWindow = new PhoneWindow(this);
        mWindow.setCallback(this);
        mWindow.setTitle(title);
        mWindow.setWindowManager(new android.view.WindowManagerImpl(this), mToken, component.flattenToString());
        mWindowManager = mWindow.getWindowManager();
        android.view.WindowManagerGlobal.getInstance().addAppToken(mToken);
    }

    private final android.os.Binder mToken = new android.os.Binder();
    private android.view.WindowManager mWindowManager;

    public android.view.WindowManager getWindowManager() {
        return mWindowManager;
    }

    @Override
    public Object getSystemService(String name) {
        if (WINDOW_SERVICE.equals(name)) return mWindowManager;
        if (LAYOUT_INFLATER_SERVICE.equals(name)) {
            if (mInflater == null) {
                mInflater = ((android.view.LayoutInflater) super.getSystemService(name)).cloneInContext(this);
            }
            return mInflater;
        }
        return super.getSystemService(name);
    }

    private android.view.LayoutInflater mInflater;
    private FragmentManager mFragments;

    public FragmentManager getFragmentManager() {
        if (mFragments == null) mFragments = new FragmentManager(this);
        return mFragments;
    }

    public boolean isInMultiWindowMode() {
        return false;
    }

    public boolean isInPictureInPictureMode() {
        return false;
    }

    public final void requestPermissions(final String[] permissions, final int requestCode) {
        final int[] results = new int[permissions.length];
        for (int i = 0; i < permissions.length; i++) results[i] = checkSelfPermission(permissions[i]);
        mHandler.post(new Runnable() {
            public void run() {
                onRequestPermissionsResult(requestCode, permissions, results);
            }
        });
    }

    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {}

    public boolean shouldShowRequestPermissionRationale(String permission) {
        return false;
    }

    public void startActivityForResult(Intent intent, int requestCode) {
        startActivityForResult(intent, requestCode, null);
    }

    public void startActivityForResult(Intent intent, int requestCode, Bundle options) {
        mThread.startActivity(this, intent, requestCode);
    }

    @Override
    public void startActivity(Intent intent) {
        startActivityForResult(intent, -1, null);
    }

    public void startActivity(Intent intent, Bundle options) {
        startActivityForResult(intent, -1, options);
    }

    protected void onActivityResult(int requestCode, int resultCode, Intent data) {}

    final void dispatchActivityResult(int requestCode, int resultCode, Intent data) {
        onActivityResult(requestCode, resultCode, data);
    }

    final int getResultCode() {
        return mResultCode;
    }

    final Intent getResultData() {
        return mResultData;
    }

    public View getCurrentFocus() {
        return mWindow != null ? mWindow.getDecorView().findFocus() : null;
    }

    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        onUserInteraction();
        View decor = mWindow != null ? mWindow.getDecorView() : null;
        if (decor != null && decor.dispatchKeyEvent(event)) return true;
        return event.dispatch(this, null, this);
    }

    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        return onGenericMotionEvent(ev);
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        return false;
    }

    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        return keyCode == android.view.KeyEvent.KEYCODE_BACK;
    }

    public boolean onKeyLongPress(int keyCode, android.view.KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK && !event.isCanceled()) {
            onBackPressed();
            return true;
        }
        return false;
    }

    public boolean onKeyMultiple(int keyCode, int repeatCount, android.view.KeyEvent event) {
        return false;
    }

    public void onWindowAttributesChanged(android.view.WindowManager.LayoutParams params) {}

    public void onMultiWindowModeChanged(boolean isInMultiWindowMode) {}

    public void onNewIntent(Intent intent) {}

    public void setVolumeControlStream(int streamType) {}

    public void setTaskDescription(Object taskDescription) {}

    public void overridePendingTransition(int enterAnim, int exitAnim) {}

    public android.view.LayoutInflater getLayoutInflater() {
        return (android.view.LayoutInflater) getSystemService(LAYOUT_INFLATER_SERVICE);
    }

    // --- lifecycle drivers (ActivityThread) -------------------------------------

    private void checkCalled(String method) {
        if (!mCalled) {
            throw new SuperNotCalledException("Activity " + mComponent.toShortString()
                    + " did not call through to super." + method + "()");
        }
    }

    final void performCreate(Bundle icicle) {
        mCalled = false;
        onCreate(icicle);
        checkCalled("onCreate");
    }

    final void performStart() {
        mCalled = false;
        onStart();
        checkCalled("onStart");
        mStopped = false;
        mCalled = false;
        onPostCreate(null);
        checkCalled("onPostCreate");
    }

    final void performResume() {
        mCalled = false;
        onResume();
        checkCalled("onResume");
        mResumed = true;
        if (mFragments != null) mFragments.dispatchResume();
        mCalled = false;
        onPostResume();
        checkCalled("onPostResume");
    }

    final void performPause() {
        mCalled = false;
        if (mFragments != null) mFragments.dispatchPause();
        onPause();
        checkCalled("onPause");
        mResumed = false;
    }

    final void performRestart() {
        mCalled = false;
        onRestart();
        checkCalled("onRestart");
    }

    final void performStop() {
        mCalled = false;
        onStop();
        checkCalled("onStop");
        mStopped = true;
    }

    final void performDestroy() {
        mDestroyed = true;
        android.view.WindowManagerGlobal.getInstance().removeAppToken(mToken);
        if (mFragments != null) mFragments.dispatchDestroy();
        mCalled = false;
        onDestroy();
        checkCalled("onDestroy");
    }

    // --- lifecycle callbacks ----------------------------------------------------

    protected void onCreate(Bundle savedInstanceState) {
        mApplication.dispatchActivityCreated(this, savedInstanceState);
        mCalled = true;
    }

    protected void onPostCreate(Bundle savedInstanceState) {
        mCalled = true;
    }

    protected void onStart() {
        mApplication.dispatchActivityStarted(this);
        mCalled = true;
    }

    protected void onRestart() {
        mCalled = true;
    }

    protected void onResume() {
        mApplication.dispatchActivityResumed(this);
        mCalled = true;
    }

    protected void onPostResume() {
        mCalled = true;
    }

    protected void onPause() {
        mApplication.dispatchActivityPaused(this);
        mCalled = true;
    }

    protected void onStop() {
        mApplication.dispatchActivityStopped(this);
        mCalled = true;
    }

    protected void onDestroy() {
        mApplication.dispatchActivityDestroyed(this);
        mCalled = true;
    }

    protected void onSaveInstanceState(Bundle outState) {}

    public void onSaveInstanceState(Bundle outState, android.os.PersistableBundle outPersistentState) {
        onSaveInstanceState(outState);
    }

    protected void onRestoreInstanceState(Bundle savedInstanceState) {}

    public void onConfigurationChanged(Configuration newConfig) {
        mCalled = true;
    }

    public void onLowMemory() {
        mCalled = true;
    }

    public void onTrimMemory(int level) {
        mCalled = true;
    }

    // --- Window.Callback ----------------------------------------------------------

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (ev.getAction() == MotionEvent.ACTION_DOWN) onUserInteraction();
        if (getWindow().superDispatchTouchEvent(ev)) return true;
        return onTouchEvent(ev);
    }

    public boolean onTouchEvent(MotionEvent event) {
        return false;
    }

    public void onUserInteraction() {}

    @Override
    public void onContentChanged() {}

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {}

    @Override
    public void onAttachedToWindow() {}

    @Override
    public void onDetachedFromWindow() {}

    public boolean hasWindowFocus() {
        return mResumed && !mFinished;
    }

    // --- window and content ---------------------------------------------------------

    public android.view.Display getDisplay() {
        return getWindowManager().getDefaultDisplay();
    }

    public Window getWindow() {
        return mWindow;
    }

    public void setContentView(int layoutResID) {
        getWindow().setContentView(layoutResID);
    }

    public void setContentView(View view) {
        getWindow().setContentView(view);
    }

    public void setContentView(View view, ViewGroup.LayoutParams params) {
        getWindow().setContentView(view, params);
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
        getWindow().addContentView(view, params);
    }

    public <T extends View> T findViewById(int id) {
        return getWindow().findViewById(id);
    }

    public final boolean requestWindowFeature(int featureId) {
        return getWindow().requestFeature(featureId);
    }

    public void setTitle(CharSequence title) {
        mTitle = title;
        mWindow.setTitle(title);
        if (mThread != null) mThread.onActivityTitleChanged(this, title);
    }

    public void setTitle(int titleId) {
        setTitle(getText(titleId));
    }

    public final CharSequence getTitle() {
        return mTitle;
    }

    public void setRequestedOrientation(int requestedOrientation) {
        mRequestedOrientation = requestedOrientation;
    }

    public int getRequestedOrientation() {
        return mRequestedOrientation;
    }

    // --- identity -------------------------------------------------------------------

    public final Application getApplication() {
        return mApplication;
    }

    public Intent getIntent() {
        return mIntent;
    }

    public void setIntent(Intent newIntent) {
        mIntent = newIntent;
    }

    public ComponentName getComponentName() {
        return mComponent;
    }

    public String getLocalClassName() {
        final String pkg = getPackageName();
        final String cls = mComponent.getClassName();
        int packageLen = pkg.length();
        if (!cls.startsWith(pkg) || cls.length() <= packageLen || cls.charAt(packageLen) != '.') return cls;
        return cls.substring(packageLen + 1);
    }

    public ComponentName getCallingActivity() {
        return mCallingActivity;
    }

    public String getCallingPackage() {
        return null;
    }

    public boolean isTaskRoot() {
        return true;
    }

    public SharedPreferences getPreferences(int mode) {
        return getSharedPreferences(getLocalClassName(), mode);
    }

    // --- finishing ----------------------------------------------------------------

    public void finish() {
        if (mFinished) return;
        mFinished = true;
        mThread.scheduleFinish(this);
    }

    public void finishAffinity() {
        finish();
    }

    public boolean isFinishing() {
        return mFinished;
    }

    public boolean isDestroyed() {
        return mDestroyed;
    }

    public boolean isChangingConfigurations() {
        return false;
    }

    public void onBackPressed() {
        finish();
    }

    public final void setResult(int resultCode) {
        mResultCode = resultCode;
        mResultData = null;
    }

    public final void setResult(int resultCode, Intent data) {
        mResultCode = resultCode;
        mResultData = data;
    }

    // --- threading --------------------------------------------------------------------

    public final void runOnUiThread(Runnable action) {
        if (Thread.currentThread() != mUiThread) {
            mHandler.post(action);
        } else {
            action.run();
        }
    }

    @Override
    public String toString() {
        return getClass().getName() + "@" + Integer.toHexString(System.identityHashCode(this));
    }
}
