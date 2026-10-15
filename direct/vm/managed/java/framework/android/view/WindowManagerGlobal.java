package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;
import java.util.ArrayList;
import java.util.HashSet;

/**
 * The session's window manager: every window added by the application, its
 * z-order, window focus and input routing. The desktop host shows one
 * surface; all windows are composed into it bottom to top each frame, with
 * the dim of FLAG_DIM_BEHIND windows drawn beneath them.
 *
 * Runtime-internal (hidden on Android).
 */
public final class WindowManagerGlobal {
    private static final String TAG = "WindowManager";
    private static WindowManagerGlobal sInstance;

    /** Receives requests that concern the whole session. */
    public interface SessionCallbacks {
        /** The host window was closed or the session timed out. */
        void onCloseRequested();
    }

    private final ArrayList<ViewRootImpl> mRoots = new ArrayList<ViewRootImpl>();
    private final HashSet<IBinder> mAppTokens = new HashSet<IBinder>();
    private SessionCallbacks mSessionCallbacks;
    private long mSurface;
    private int mDisplayWidth;
    private int mDisplayHeight;
    private boolean mInTouchMode = true;
    private ViewRootImpl mFocusedWindow;
    private ViewRootImpl mPointerTarget;
    private int mNextSeq;
    private boolean mFrameScheduled;
    private CharSequence mHostTitle = "";
    private final InsetsController mInsetsController = new InsetsController();

    private final Runnable mFrameRunnable = new Runnable() {
        public void run() {
            mFrameScheduled = false;
            performFrame();
        }
    };

    private WindowManagerGlobal() {
        android.util.DisplayMetrics m = android.content.res.Resources.getSystem().getDisplayMetrics();
        mDisplayWidth = m.widthPixels;
        mDisplayHeight = m.heightPixels;
    }

    public static synchronized WindowManagerGlobal getInstance() {
        if (sInstance == null) sInstance = new WindowManagerGlobal();
        return sInstance;
    }

    public void setSessionCallbacks(SessionCallbacks callbacks) {
        mSessionCallbacks = callbacks;
    }

    // --- application tokens ---------------------------------------------------------------------------

    /** Registers the token of a running activity; application windows must carry one. */
    public void addAppToken(IBinder token) {
        mAppTokens.add(token);
    }

    public void removeAppToken(IBinder token) {
        mAppTokens.remove(token);
    }

    /** Removes the windows left behind by a destroyed activity, as Android does for leaked windows. */
    public void closeAll(IBinder token, String who, String what) {
        for (ViewRootImpl root : new ArrayList<ViewRootImpl>(mRoots)) {
            if (root.mWindowAttributes.token == token && root.mView != null) {
                Log.e(TAG, what + " " + who + " has leaked window " + root.mView + " that was originally added here");
                removeViewLocked(root, true);
            }
        }
    }

    // --- adding and removing windows -------------------------------------------------------------------

    public void addView(View view, ViewGroup.LayoutParams params, Display display, Window parentWindow) {
        if (view == null) throw new IllegalArgumentException("view must not be null");
        if (!(params instanceof WindowManager.LayoutParams)) {
            throw new IllegalArgumentException("Params must be WindowManager.LayoutParams");
        }
        final WindowManager.LayoutParams wparams = (WindowManager.LayoutParams) params;
        if (parentWindow != null) parentWindow.adjustLayoutParamsForSubWindow(wparams);
        if (findRoot(view) != null) {
            throw new IllegalStateException("View " + view + " has already been added to the window manager.");
        }
        final int type = wparams.type;
        ViewRootImpl parentRoot = null;
        if (type >= WindowManager.LayoutParams.FIRST_SUB_WINDOW && type <= WindowManager.LayoutParams.LAST_SUB_WINDOW) {
            for (ViewRootImpl r : mRoots) {
                if (r.mWindowToken == wparams.token) parentRoot = r;
            }
            if (parentRoot == null) {
                throw new WindowManager.BadTokenException("Unable to add window -- token " + wparams.token
                        + " is not valid; is your activity running?");
            }
        } else if (type >= WindowManager.LayoutParams.FIRST_APPLICATION_WINDOW
                && type <= WindowManager.LayoutParams.LAST_APPLICATION_WINDOW) {
            if (wparams.token == null) {
                throw new WindowManager.BadTokenException(
                        "Unable to add window -- token null is not for an application");
            }
            if (!(wparams.token instanceof IBinder) || !mAppTokens.contains(wparams.token)) {
                throw new WindowManager.BadTokenException("Unable to add window -- token " + wparams.token
                        + " is not valid; is your activity running?");
            }
        } else if (type != WindowManager.LayoutParams.TYPE_TOAST) {
            // System window types need permissions an application does not hold here.
            throw new WindowManager.BadTokenException("Unable to add window -- permission denied for window type "
                    + type);
        }

        ViewRootImpl root = new ViewRootImpl(view.getContext());
        root.mSeq = ++mNextSeq;
        root.mParentWindow = parentRoot;
        view.setLayoutParams(wparams);
        mRoots.add(root);
        sortWindows();
        ensureSurface(wparams.getTitle());
        root.setView(view, wparams);
        scheduleFrame();
    }

    public void updateViewLayout(View view, ViewGroup.LayoutParams params) {
        if (view == null) throw new IllegalArgumentException("view must not be null");
        if (!(params instanceof WindowManager.LayoutParams)) {
            throw new IllegalArgumentException("Params must be WindowManager.LayoutParams");
        }
        final WindowManager.LayoutParams wparams = (WindowManager.LayoutParams) params;
        ViewRootImpl root = findRoot(view);
        if (root == null) throw new IllegalArgumentException("View=" + view + " not attached to window manager");
        view.setLayoutParams(wparams);
        root.setLayoutParams(wparams);
    }

    public void removeView(View view, boolean immediate) {
        if (view == null) throw new IllegalArgumentException("view must not be null");
        ViewRootImpl root = findRoot(view);
        if (root == null) throw new IllegalArgumentException("View=" + view + " not attached to window manager");
        removeViewLocked(root, immediate);
    }

    private void removeViewLocked(ViewRootImpl root, boolean immediate) {
        // Child windows go with their parent.
        for (ViewRootImpl r : new ArrayList<ViewRootImpl>(mRoots)) {
            if (r.mParentWindow == root) removeViewLocked(r, immediate);
        }
        if (!mRoots.remove(root)) return;
        if (mPointerTarget == root) mPointerTarget = null;
        if (mFocusedWindow == root) {
            root.windowFocusChanged(false);
            mFocusedWindow = null;
        }
        root.die();
        updateFocusedWindow();
        scheduleFrame();
    }

    ViewRootImpl findRoot(View view) {
        for (ViewRootImpl r : mRoots) {
            if (r.mView == view) return r;
        }
        return null;
    }

    public View[] getRootViews() {
        View[] views = new View[mRoots.size()];
        for (int i = 0; i < views.length; i++) views[i] = mRoots.get(i).mView;
        return views;
    }

    /** Z-order: top-level windows by layer then age; each window's sub-windows right above it. */
    private void sortWindows() {
        ArrayList<ViewRootImpl> topLevel = new ArrayList<ViewRootImpl>();
        for (ViewRootImpl r : mRoots) {
            if (r.mParentWindow == null) topLevel.add(r);
        }
        sortByLayer(topLevel);
        ArrayList<ViewRootImpl> ordered = new ArrayList<ViewRootImpl>(mRoots.size());
        for (ViewRootImpl r : topLevel) appendWithChildren(r, ordered);
        mRoots.clear();
        mRoots.addAll(ordered);
    }

    private void appendWithChildren(ViewRootImpl root, ArrayList<ViewRootImpl> out) {
        out.add(root);
        ArrayList<ViewRootImpl> children = new ArrayList<ViewRootImpl>();
        for (ViewRootImpl r : mRoots) {
            if (r.mParentWindow == root) children.add(r);
        }
        sortByLayer(children);
        for (ViewRootImpl c : children) appendWithChildren(c, out);
    }

    private static void sortByLayer(ArrayList<ViewRootImpl> list) {
        for (int i = 1; i < list.size(); i++) {
            ViewRootImpl v = list.get(i);
            int j = i - 1;
            while (j >= 0 && compareWindows(list.get(j), v) > 0) {
                list.set(j + 1, list.get(j));
                j--;
            }
            list.set(j + 1, v);
        }
    }

    private static int compareWindows(ViewRootImpl a, ViewRootImpl b) {
        int la = layerOf(a.mWindowAttributes.type);
        int lb = layerOf(b.mWindowAttributes.type);
        if (la != lb) return la < lb ? -1 : 1;
        return a.mSeq < b.mSeq ? -1 : (a.mSeq == b.mSeq ? 0 : 1);
    }

    private static int layerOf(int type) {
        switch (type) {
            case WindowManager.LayoutParams.TYPE_APPLICATION_MEDIA:
                return -1;
            case WindowManager.LayoutParams.TYPE_APPLICATION_PANEL:
            case WindowManager.LayoutParams.TYPE_APPLICATION_ATTACHED_DIALOG:
                return 1;
            case WindowManager.LayoutParams.TYPE_APPLICATION_SUB_PANEL:
                return 2;
            case WindowManager.LayoutParams.TYPE_TOAST:
                return 10;
            default:
                return 0;
        }
    }

    void onWindowAttributesChanged(ViewRootImpl root) {
        sortWindows();
        updateFocusedWindow();
    }

    void onWindowVisibilityChanged(ViewRootImpl root) {
        if (mPointerTarget == root && !root.isVisibleWindow()) mPointerTarget = null;
        updateFocusedWindow();
    }

    // --- the host surface ------------------------------------------------------------------------------

    private void ensureSurface(CharSequence title) {
        if (mSurface != 0) return;
        int[] size = new int[2];
        mSurface = ViewRootImpl.nativeOpenWindow(mHostTitle.length() > 0 ? mHostTitle.toString()
                : (title != null ? title.toString() : ""), size);
        mDisplayWidth = size[0];
        mDisplayHeight = size[1];
    }

    /** The host window's title: the label of the task's top activity. */
    public void setHostTitle(CharSequence title) {
        mHostTitle = title == null ? "" : title;
        if (mSurface != 0) ViewRootImpl.nativeSetTitle(mSurface, mHostTitle.toString());
    }

    /** Closes the host window at the end of the session. */
    public void closeHostWindow() {
        for (ViewRootImpl root : new ArrayList<ViewRootImpl>(mRoots)) removeViewLocked(root, true);
        if (mSurface != 0) {
            ViewRootImpl.nativeCloseWindow(mSurface);
            mSurface = 0;
        }
    }

    void getDisplayFrame(Rect outRect) {
        outRect.set(0, 0, mDisplayWidth, mDisplayHeight);
    }

    public int getDisplayWidth() {
        return mDisplayWidth;
    }

    public int getDisplayHeight() {
        return mDisplayHeight;
    }

    WindowInsetsController getInsetsController() {
        return mInsetsController;
    }

    // --- frames ---------------------------------------------------------------------------------------------

    void scheduleFrame() {
        if (!mFrameScheduled) {
            mFrameScheduled = true;
            Choreographer.getInstance().postCallback(Choreographer.CALLBACK_TRAVERSAL, mFrameRunnable, null);
        }
    }

    /** Lays out every window that needs it, then composes and presents all visible windows. */
    void performFrame() {
        final int dw = mDisplayWidth;
        final int dh = mDisplayHeight;
        for (ViewRootImpl root : new ArrayList<ViewRootImpl>(mRoots)) {
            if (root.isAdded()) root.performTraversals(dw, dh);
        }
        updateFocusedWindow();
        if (mSurface == 0) return;
        final long drawingTime = SystemClock.uptimeMillis();
        Canvas canvas = new Canvas(mSurface, dw, dh);
        canvas.drawColor(0xFF000000);
        for (ViewRootImpl root : new ArrayList<ViewRootImpl>(mRoots)) {
            if (!root.isVisibleWindow()) continue;
            final WindowManager.LayoutParams lp = root.mWindowAttributes;
            if ((lp.flags & WindowManager.LayoutParams.FLAG_DIM_BEHIND) != 0) {
                int alpha = (int) (Math.max(0f, Math.min(1f, lp.dimAmount)) * 255 + 0.5f);
                canvas.drawColor(alpha << 24);
            }
            final Rect frame = root.getFrame();
            final int save = canvas.save();
            canvas.translate(frame.left, frame.top);
            root.draw(canvas, drawingTime);
            canvas.restoreToCount(save);
        }
        ViewRootImpl.nativePresent(mSurface);
    }

    // --- focus and touch mode ------------------------------------------------------------------------------

    boolean isInTouchMode() {
        return mInTouchMode;
    }

    boolean setTouchMode(boolean inTouchMode) {
        if (mInTouchMode == inTouchMode) return false;
        mInTouchMode = inTouchMode;
        for (ViewRootImpl root : new ArrayList<ViewRootImpl>(mRoots)) root.onTouchModeChanged(inTouchMode);
        return true;
    }

    private void updateFocusedWindow() {
        ViewRootImpl focus = null;
        for (int i = mRoots.size() - 1; i >= 0; i--) {
            ViewRootImpl r = mRoots.get(i);
            if (r.isFocusableWindow()) {
                focus = r;
                break;
            }
        }
        if (focus == mFocusedWindow) return;
        ViewRootImpl old = mFocusedWindow;
        mFocusedWindow = focus;
        if (old != null) old.windowFocusChanged(false);
        if (focus != null) focus.windowFocusChanged(true);
    }

    // --- input from the host -------------------------------------------------------------------------------

    void dispatchPointer(int action, float x, float y, long eventTime) {
        if (action == MotionEvent.ACTION_DOWN) {
            if (mPointerTarget != null) {
                mPointerTarget.deliverPointerEvent(MotionEvent.ACTION_CANCEL, x, y, eventTime);
                mPointerTarget = null;
            }
            setTouchMode(true);
            mPointerTarget = findTouchTarget(x, y, eventTime);
        }
        ViewRootImpl target = mPointerTarget;
        if (target == null || !mRoots.contains(target)) return;
        target.deliverPointerEvent(action, x, y, eventTime);
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) mPointerTarget = null;
    }

    /**
     * AOSP touch routing: the topmost touchable window containing the point, or
     * a touch-modal window above it; windows passed over that watch outside
     * touches get ACTION_OUTSIDE.
     */
    private ViewRootImpl findTouchTarget(float x, float y, long eventTime) {
        for (int i = mRoots.size() - 1; i >= 0; i--) {
            ViewRootImpl r = mRoots.get(i);
            if (!r.isTouchableWindow()) continue;
            final int flags = r.mWindowAttributes.flags;
            final boolean inside = r.getFrame().contains((int) x, (int) y);
            final boolean touchModal = (flags & (WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL)) == 0;
            if (inside || touchModal) return r;
            if ((flags & WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH) != 0) {
                r.deliverOutsideTouch(x, y, eventTime);
            }
        }
        return null;
    }

    void dispatchScroll(float x, float y, float vscroll, long eventTime) {
        ViewRootImpl target = findTouchTarget(x, y, eventTime);
        if (target != null) target.deliverScroll(x, y, vscroll, eventTime);
    }

    void dispatchKey(int action, int keyCode, int unicode, int metaState, long eventTime) {
        KeyEvent event = KeyEvent.fromHost(action, keyCode, unicode, metaState, eventTime);
        deliverKey(event);
    }

    private void deliverKey(KeyEvent event) {
        final ViewRootImpl target = mFocusedWindow;
        if (target == null) return;
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (isNavigationKey(event)) {
                // The first navigation key only leaves touch mode (and shows focus).
                if (setTouchMode(false)) return;
            } else if (event.getUnicodeChar() > 0) {
                setTouchMode(false);
            }
        }
        target.deliverKeyEvent(event);
    }

    private static boolean isNavigationKey(KeyEvent keyEvent) {
        switch (keyEvent.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_LEFT:
            case KeyEvent.KEYCODE_DPAD_RIGHT:
            case KeyEvent.KEYCODE_DPAD_UP:
            case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_PAGE_UP:
            case KeyEvent.KEYCODE_PAGE_DOWN:
            case KeyEvent.KEYCODE_MOVE_HOME:
            case KeyEvent.KEYCODE_MOVE_END:
            case KeyEvent.KEYCODE_TAB:
            case KeyEvent.KEYCODE_SPACE:
            case KeyEvent.KEYCODE_ENTER:
                return true;
        }
        return false;
    }

    /** The host's back gesture (Escape, browser-back): a BACK key press to the focused window. */
    void dispatchBack() {
        long now = SystemClock.uptimeMillis();
        deliverKey(new KeyEvent(now, now, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK, 0));
        deliverKey(new KeyEvent(now, now, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK, 0));
    }

    void dispatchResize(int width, int height) {
        if (width == mDisplayWidth && height == mDisplayHeight) {
            scheduleFrame();
            return;
        }
        mDisplayWidth = width;
        mDisplayHeight = height;
        android.content.res.Resources.onDisplaySizeChanged(width, height);
        for (ViewRootImpl root : mRoots) root.requestLayout();
        scheduleFrame();
    }

    void dispatchCloseRequest() {
        if (mSessionCallbacks != null) {
            mSessionCallbacks.onCloseRequested();
        } else {
            closeHostWindow();
        }
    }

    /**
     * Window insets requests. The desktop host shows no status or navigation
     * bars, so there is nothing to show or hide; requests are recorded only.
     */
    static final class InsetsController implements WindowInsetsController {
        private int mAppearance;
        private int mBehavior = BEHAVIOR_DEFAULT;

        public void show(int types) {}

        public void hide(int types) {}

        public void setSystemBarsAppearance(int appearance, int mask) {
            mAppearance = (mAppearance & ~mask) | (appearance & mask);
        }

        public int getSystemBarsAppearance() {
            return mAppearance;
        }

        public void setSystemBarsBehavior(int behavior) {
            mBehavior = behavior;
        }

        public int getSystemBarsBehavior() {
            return mBehavior;
        }

        public void addOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener) {}

        public void removeOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener) {}
    }
}
