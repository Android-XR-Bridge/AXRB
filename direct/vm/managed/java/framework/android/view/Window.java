package android.view;

import android.content.Context;
import android.util.AndroidRuntimeException;

public abstract class Window {
    public static final int FEATURE_OPTIONS_PANEL = 0;
    public static final int FEATURE_NO_TITLE = 1;
    public static final int FEATURE_PROGRESS = 2;
    public static final int FEATURE_LEFT_ICON = 3;
    public static final int FEATURE_RIGHT_ICON = 4;
    public static final int FEATURE_INDETERMINATE_PROGRESS = 5;
    public static final int FEATURE_CONTEXT_MENU = 6;
    public static final int FEATURE_CUSTOM_TITLE = 7;
    public static final int FEATURE_ACTION_BAR = 8;
    public static final int FEATURE_ACTION_BAR_OVERLAY = 9;
    public static final int FEATURE_ACTION_MODE_OVERLAY = 10;

    /** android.R.id.content */
    public static final int ID_ANDROID_CONTENT = 0x01020002;

    public interface Callback {
        boolean dispatchTouchEvent(MotionEvent event);

        void onContentChanged();

        void onWindowFocusChanged(boolean hasFocus);

        void onAttachedToWindow();

        void onDetachedFromWindow();
    }

    private final Context mContext;
    private final WindowManager.LayoutParams mWindowAttributes = new WindowManager.LayoutParams();
    private WindowManager mWindowManager;
    private Callback mCallback;
    private int mFeatures;
    private int mLocalFeatures;
    private int mFlags;
    private int mStatusBarColor;
    private int mNavigationBarColor;
    private int mSoftInputMode;
    protected boolean mContentSet;

    public Window(Context context) {
        mContext = context;
    }

    public final Context getContext() {
        return mContext;
    }

    public void setCallback(Callback callback) {
        mCallback = callback;
    }

    public final Callback getCallback() {
        return mCallback;
    }

    public boolean requestFeature(int featureId) {
        if (mContentSet) throw new AndroidRuntimeException("requestFeature() must be called before adding content");
        final int flag = 1 << featureId;
        mFeatures |= flag;
        mLocalFeatures |= flag;
        if (featureId == FEATURE_NO_TITLE) {
            mFeatures &= ~(1 << FEATURE_ACTION_BAR);
            mLocalFeatures &= ~(1 << FEATURE_ACTION_BAR);
        } else if (featureId == FEATURE_ACTION_BAR) {
            mFeatures &= ~(1 << FEATURE_NO_TITLE);
            mLocalFeatures &= ~(1 << FEATURE_NO_TITLE);
        }
        return true;
    }

    public boolean hasFeature(int feature) {
        return (mFeatures & (1 << feature)) != 0;
    }

    protected final int getFeatures() {
        return mFeatures;
    }

    public void addFlags(int flags) {
        setFlags(flags, flags);
    }

    public void clearFlags(int flags) {
        setFlags(0, flags);
    }

    public void setFlags(int flags, int mask) {
        mFlags = (mFlags & ~mask) | (flags & mask);
        mWindowAttributes.flags = (mWindowAttributes.flags & ~mask) | (flags & mask);
        dispatchWindowAttributesChanged();
    }

    public final WindowManager.LayoutParams getAttributes() {
        return mWindowAttributes;
    }

    public void setAttributes(WindowManager.LayoutParams a) {
        mWindowAttributes.copyFrom(a);
        mFlags = mWindowAttributes.flags;
        dispatchWindowAttributesChanged();
    }

    public void setLayout(int width, int height) {
        mWindowAttributes.width = width;
        mWindowAttributes.height = height;
        dispatchWindowAttributesChanged();
    }

    public void setGravity(int gravity) {
        mWindowAttributes.gravity = gravity;
        dispatchWindowAttributesChanged();
    }

    public void setType(int type) {
        mWindowAttributes.type = type;
    }

    public void setDimAmount(float amount) {
        mWindowAttributes.dimAmount = amount;
    }

    public void setBackgroundDrawable(android.graphics.drawable.Drawable drawable) {}

    public void setBackgroundDrawableResource(int resId) {}

    public void setWindowManager(WindowManager wm, android.os.IBinder appToken, String appName) {
        mAppToken = appToken;
        mWindowManager = ((WindowManagerImpl) wm).createLocalWindowManager(this);
    }

    public WindowManager getWindowManager() {
        return mWindowManager;
    }

    private android.os.IBinder mAppToken;

    /** Sub-windows (dialogs, popups) of this window share its token. */
    void adjustLayoutParamsForSubWindow(WindowManager.LayoutParams wp) {
        if (wp.token == null) wp.token = mAppToken;
    }

    protected void dispatchWindowAttributesChanged() {
        View decor = peekDecorView();
        if (decor != null && decor.getParent() != null && mWindowManager != null) {
            mWindowManager.updateViewLayout(decor, mWindowAttributes);
        }
    }

    public boolean isActive() {
        return peekDecorView() != null;
    }

    public void setSoftInputMode(int mode, boolean unused) {
        setSoftInputMode(mode);
    }

    public android.view.WindowInsetsController getInsetsController() {
        return null;
    }

    public void setDecorFitsSystemWindows(boolean decorFitsSystemWindows) {}

    public void setSustainedPerformanceMode(boolean enable) {}

    public void setPreferMinimalPostProcessing(boolean isPreferred) {}

    public void setColorMode(int colorMode) {}

    public int getColorMode() {
        return 0;
    }

    public void setHideOverlayWindows(boolean hide) {}

    public void setNavigationBarContrastEnforced(boolean enforce) {}

    public void setStatusBarContrastEnforced(boolean enforce) {}

    public void takeKeyEvents(boolean get) {}

    /** Gives the window's surface to callback instead of the view hierarchy. */
    public void takeSurface(SurfaceHolder.Callback2 callback) {}

    public void takeInputQueue(InputQueue.Callback callback) {}

    public void setWindowAnimations(int resId) {}

    public void setFormat(int format) {
        mWindowAttributes.format = format;
    }

    public int getFlags() {
        return mFlags;
    }

    public void setStatusBarColor(int color) {
        mStatusBarColor = color;
    }

    public int getStatusBarColor() {
        return mStatusBarColor;
    }

    public void setNavigationBarColor(int color) {
        mNavigationBarColor = color;
    }

    public int getNavigationBarColor() {
        return mNavigationBarColor;
    }

    public void setSoftInputMode(int mode) {
        mSoftInputMode = mode;
        mWindowAttributes.softInputMode = mode;
    }

    public <T extends View> T findViewById(int id) {
        return getDecorView().findViewById(id);
    }

    public abstract void setContentView(int layoutResID);

    public abstract void setContentView(View view);

    public abstract void setContentView(View view, ViewGroup.LayoutParams params);

    public abstract void addContentView(View view, ViewGroup.LayoutParams params);

    public abstract View getDecorView();

    public abstract View peekDecorView();

    public abstract void setTitle(CharSequence title);

    public abstract boolean superDispatchTouchEvent(MotionEvent event);

    public abstract void setBackgroundColor(int color);
}
