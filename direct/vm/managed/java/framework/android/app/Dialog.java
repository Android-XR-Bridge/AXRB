package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import com.android.internal.policy.PhoneWindow;

/**
 * A dialog window. Dialogs are added to the window manager like on Android;
 * there is no visible decoration (the runner shows the activity window only),
 * but the lifecycle and listener contract hold.
 */
public class Dialog implements DialogInterface, Window.Callback, KeyEvent.Callback {
    private static final String TAG = "Dialog";

    private final Context mContext;
    private final PhoneWindow mWindow;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mShowing;
    private boolean mCreated;
    private boolean mCancelable = true;
    private boolean mCanceledOnTouchOutside;
    private OnCancelListener mOnCancel;
    private OnDismissListener mOnDismiss;
    private OnShowListener mOnShow;
    private OnKeyListener mOnKey;
    private Activity mOwnerActivity;

    public Dialog(Context context) {
        this(context, 0);
    }

    public Dialog(Context context, int themeResId) {
        mContext = context;
        mWindow = new PhoneWindow(context);
        mWindow.setCallback(this);
        if (context instanceof Activity) mOwnerActivity = (Activity) context;
    }

    protected Dialog(Context context, boolean cancelable, OnCancelListener cancelListener) {
        this(context);
        mCancelable = cancelable;
        mOnCancel = cancelListener;
    }

    public final Context getContext() {
        return mContext;
    }

    public final Activity getOwnerActivity() {
        return mOwnerActivity;
    }

    public final void setOwnerActivity(Activity activity) {
        mOwnerActivity = activity;
    }

    public Window getWindow() {
        return mWindow;
    }

    public <T extends View> T findViewById(int id) {
        return mWindow.findViewById(id);
    }

    public void setContentView(int layoutResID) {
        mWindow.setContentView(layoutResID);
    }

    public void setContentView(View view) {
        mWindow.setContentView(view);
    }

    public void setContentView(View view, ViewGroup.LayoutParams params) {
        mWindow.setContentView(view, params);
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
        mWindow.addContentView(view, params);
    }

    public void setTitle(CharSequence title) {
        mWindow.setTitle(title);
    }

    public void setTitle(int titleId) {
        setTitle(mContext.getText(titleId));
    }

    public final boolean requestWindowFeature(int featureId) {
        return mWindow.requestFeature(featureId);
    }

    protected void onCreate(android.os.Bundle savedInstanceState) {}

    protected void onStart() {}

    protected void onStop() {}

    void dispatchOnCreate(android.os.Bundle savedInstanceState) {
        if (!mCreated) {
            onCreate(savedInstanceState);
            mCreated = true;
        }
    }

    public void create() {
        dispatchOnCreate(null);
    }

    public void show() {
        if (mShowing) return;
        dispatchOnCreate(null);
        onStart();
        mShowing = true;
        Log.i(TAG, "dialog shown: " + mWindow.getTitle());
        if (mOnShow != null) {
            final OnShowListener l = mOnShow;
            mHandler.post(new Runnable() {
                public void run() {
                    l.onShow(Dialog.this);
                }
            });
        }
    }

    public boolean isShowing() {
        return mShowing;
    }

    public void hide() {}

    public void dismiss() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mHandler.post(new Runnable() {
                public void run() {
                    dismiss();
                }
            });
            return;
        }
        if (!mShowing) return;
        mShowing = false;
        onStop();
        if (mOnDismiss != null) mOnDismiss.onDismiss(this);
    }

    public void cancel() {
        if (mOnCancel != null) mOnCancel.onCancel(this);
        dismiss();
    }

    public void setCancelable(boolean flag) {
        mCancelable = flag;
    }

    public void setCanceledOnTouchOutside(boolean cancel) {
        mCanceledOnTouchOutside = cancel;
    }

    public void setOnCancelListener(OnCancelListener listener) {
        mOnCancel = listener;
    }

    public void setOnDismissListener(OnDismissListener listener) {
        mOnDismiss = listener;
    }

    public void setOnShowListener(OnShowListener listener) {
        mOnShow = listener;
    }

    public void setOnKeyListener(OnKeyListener onKeyListener) {
        mOnKey = onKeyListener;
    }

    public void onBackPressed() {
        if (mCancelable) cancel();
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && mCancelable) {
            onBackPressed();
            return true;
        }
        return false;
    }

    public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
        return false;
    }

    public boolean onTouchEvent(MotionEvent event) {
        return false;
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (mOnKey != null && mOnKey.onKey(this, event.getKeyCode(), event)) return true;
        return event.dispatch(this, null, this);
    }

    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (mWindow.superDispatchTouchEvent(ev)) return true;
        return onTouchEvent(ev);
    }

    public void onWindowFocusChanged(boolean hasFocus) {}

    public void onAttachedToWindow() {}

    public void onDetachedFromWindow() {}

    public void onContentChanged() {}

    public void onWindowAttributesChanged(WindowManager.LayoutParams params) {}

    public LayoutInflater getLayoutInflater() {
        return LayoutInflater.from(mContext);
    }
}
