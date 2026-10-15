package android.view.inputmethod;

import android.os.IBinder;
import android.os.ResultReceiver;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewRootImpl;
import java.util.Collections;
import java.util.List;

/**
 * The application's view of the input method. The desktop host has a
 * physical keyboard and no soft input method: text editors receive key
 * events directly, requests to show a soft keyboard report that none was
 * shown, and no input method is listed as installed.
 */
public final class InputMethodManager {
    public static final int SHOW_IMPLICIT = 0x0001;
    public static final int SHOW_FORCED = 0x0002;
    public static final int HIDE_IMPLICIT_ONLY = 0x0001;
    public static final int HIDE_NOT_ALWAYS = 0x0002;
    public static final int RESULT_UNCHANGED_SHOWN = 0;
    public static final int RESULT_UNCHANGED_HIDDEN = 1;
    public static final int RESULT_SHOWN = 2;
    public static final int RESULT_HIDDEN = 3;

    private static InputMethodManager sInstance;

    private View mServedView;
    private View mCurRootView;

    private InputMethodManager() {}

    public static InputMethodManager getInstance() {
        synchronized (InputMethodManager.class) {
            if (sInstance == null) sInstance = new InputMethodManager();
            return sInstance;
        }
    }

    /** The instance if one was created (hidden on Android). */
    public static InputMethodManager peekInstance() {
        synchronized (InputMethodManager.class) {
            return sInstance;
        }
    }

    // --- window and focus tracking (called by the view system) ---------------------------------------

    public void onWindowFocus(View rootView, View focusedView) {
        mCurRootView = rootView;
        mServedView = focusedView != null && focusedView.onCheckIsTextEditor() ? focusedView : null;
    }

    public void onWindowLostFocus(View rootView) {
        if (mCurRootView == rootView) {
            mCurRootView = null;
            mServedView = null;
        }
    }

    /** A view gained or lost focus. */
    public void focusIn(View view) {
        if (view != null && view.onCheckIsTextEditor() && view.getRootView() == mCurRootView) mServedView = view;
    }

    public void focusOut(View view) {
        if (mServedView == view) mServedView = null;
    }

    /** Keys go straight to the focused view; no input method consumes them first. */
    public boolean dispatchKeyEvent(ViewRootImpl root, KeyEvent event) {
        return false;
    }

    // --- public API ---------------------------------------------------------------------------------------

    public boolean isActive(View view) {
        return view != null && view == mServedView;
    }

    public InputMethodSubtype getCurrentInputMethodSubtype() {
        return new InputMethodSubtype("en_US", "keyboard", "");
    }

    public boolean isActive() {
        return mServedView != null;
    }

    public boolean isAcceptingText() {
        return mServedView != null;
    }

    public boolean isFullscreenMode() {
        return false;
    }

    public void restartInput(View view) {}

    public void updateSelection(View view, int selStart, int selEnd, int candidatesStart, int candidatesEnd) {}

    public void displayCompletions(View view, Object[] completions) {}

    public void viewClicked(View view) {}

    public boolean showSoftInput(View view, int flags) {
        return showSoftInput(view, flags, null);
    }

    /** No soft input method exists on this host; nothing is shown. */
    public boolean showSoftInput(View view, int flags, ResultReceiver resultReceiver) {
        if (resultReceiver != null) resultReceiver.send(RESULT_UNCHANGED_HIDDEN, null);
        return false;
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags) {
        return hideSoftInputFromWindow(windowToken, flags, null);
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags, ResultReceiver resultReceiver) {
        if (resultReceiver != null) resultReceiver.send(RESULT_UNCHANGED_HIDDEN, null);
        return false;
    }

    public void toggleSoftInput(int showFlags, int hideFlags) {}

    public void toggleSoftInputFromWindow(IBinder windowToken, int showFlags, int hideFlags) {}

    public List<Object> getInputMethodList() {
        return Collections.emptyList();
    }

    public List<Object> getEnabledInputMethodList() {
        return Collections.emptyList();
    }

    public void showInputMethodPicker() {}
}
