package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Point;
import android.graphics.Rect;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.inputmethod.InputMethodManager;

/**
 * The top of one window's view hierarchy: its layout parameters, frame on the
 * display, traversals (measure, layout, draw) and input delivery. All windows
 * share the host surface; WindowManagerGlobal composes them in z-order.
 *
 * Runtime-internal (hidden on Android).
 */
public final class ViewRootImpl implements ViewParent {
    private static final String TAG = "ViewRootImpl";

    final Context mContext;
    final WindowManager.LayoutParams mWindowAttributes = new WindowManager.LayoutParams();
    final View.AttachInfo mAttachInfo;
    final Handler mHandler;
    final Choreographer mChoreographer;
    /** The window token handed to views (View.getWindowToken). */
    final Binder mWindowToken = new Binder();
    View mView;
    /** Sequence number of the window, for stable z-ordering among equals. */
    int mSeq;
    ViewRootImpl mParentWindow;

    private final Rect mWinFrame = new Rect();
    private boolean mFirst = true;
    private boolean mLayoutRequested;
    private boolean mInLayout;
    private boolean mAdded;
    private boolean mRemoved;
    private boolean mStopped;
    private int mViewVisibility = View.VISIBLE;
    private boolean mHasWindowFocus;
    private int mLastDisplayWidth = -1;
    private int mLastDisplayHeight = -1;
    private long mDownTime;
    private boolean mPointerDown;
    private CharSequence mTitle;

    public ViewRootImpl(Context context) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("windows can only be added on the main thread");
        }
        mContext = context;
        mHandler = new Handler(Looper.getMainLooper());
        mChoreographer = Choreographer.getInstance();
        mAttachInfo = new View.AttachInfo(this, mHandler, null, mWindowToken);
    }

    // --- window setup --------------------------------------------------------------------------------

    void setView(View view, WindowManager.LayoutParams attrs) {
        if (mView != null) throw new IllegalStateException("view already set");
        mView = view;
        mWindowAttributes.copyFrom(attrs);
        mAttachInfo.mRootView = view;
        mAttachInfo.mInTouchMode = WindowManagerGlobal.getInstance().isInTouchMode();
        mTitle = attrs.getTitle();
        mAdded = true;
        view.assignParent(this);
        requestLayout();
    }

    public View getView() {
        return mView;
    }

    public WindowManager.LayoutParams getWindowAttributes() {
        return mWindowAttributes;
    }

    void setLayoutParams(WindowManager.LayoutParams attrs) {
        final int oldSoftInput = mWindowAttributes.softInputMode;
        mWindowAttributes.copyFrom(attrs);
        if (attrs.getTitle() != null) mTitle = attrs.getTitle();
        if (mWindowAttributes.softInputMode == 0) mWindowAttributes.softInputMode = oldSoftInput;
        WindowManagerGlobal.getInstance().onWindowAttributesChanged(this);
        requestLayout();
    }

    CharSequence getTitle() {
        return mTitle;
    }

    boolean isAdded() {
        return mAdded && !mRemoved;
    }

    boolean isVisibleWindow() {
        return mAdded && !mRemoved && !mStopped && mView != null && mViewVisibility == View.VISIBLE;
    }

    boolean isFocusableWindow() {
        return isVisibleWindow() && (mWindowAttributes.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) == 0;
    }

    boolean isTouchableWindow() {
        return isVisibleWindow() && (mWindowAttributes.flags & WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE) == 0;
    }

    /** Hidden while its activity is stopped (AOSP: the app token's visibility). */
    public void setWindowStopped(boolean stopped) {
        if (mStopped != stopped) {
            mStopped = stopped;
            WindowManagerGlobal.getInstance().onWindowVisibilityChanged(this);
            scheduleTraversals();
        }
    }

    Rect getFrame() {
        return mWinFrame;
    }

    void die() {
        if (mRemoved) return;
        mRemoved = true;
        if (mView != null) {
            if (mHasWindowFocus) {
                mHasWindowFocus = false;
                mAttachInfo.mHasWindowFocus = false;
            }
            if (mView.mAttachInfo != null) {
                mAttachInfo.mTreeObserver.dispatchOnWindowAttachedChange(false);
                mView.dispatchDetachedFromWindow();
            }
            mView.assignParent(null);
        }
    }

    // --- ViewParent ----------------------------------------------------------------------------------

    @Override
    public void requestLayout() {
        mLayoutRequested = true;
        scheduleTraversals();
    }

    @Override
    public boolean isLayoutRequested() {
        return mLayoutRequested;
    }

    boolean isInLayout() {
        return mInLayout;
    }

    @Override
    public void requestTransparentRegion(View child) {}

    @Override
    public void invalidateChild(View child, Rect dirty) {
        invalidate();
    }

    @Override
    public ViewParent invalidateChildInParent(int[] location, Rect dirty) {
        invalidate();
        return null;
    }

    @Override
    public ViewParent getParent() {
        return null;
    }

    @Override
    public void requestChildFocus(View child, View focused) {
        scheduleTraversals();
    }

    @Override
    public void recomputeViewAttributes(View child) {}

    @Override
    public void clearChildFocus(View child) {
        scheduleTraversals();
    }

    @Override
    public boolean getChildVisibleRect(View child, Rect r, Point offset) {
        if (child != mView) throw new RuntimeException("child is not mView");
        return r.intersect(0, 0, mWinFrame.width(), mWinFrame.height());
    }

    @Override
    public View focusSearch(View focused, int direction) {
        if (!(mView instanceof ViewGroup)) return null;
        return FocusFinder.getInstance().findNextFocus((ViewGroup) mView, focused, direction);
    }

    @Override
    public View keyboardNavigationClusterSearch(View currentCluster, int direction) {
        return null;
    }

    @Override
    public void bringChildToFront(View child) {}

    @Override
    public void focusableViewAvailable(View v) {
        if (mView != null && !mView.hasFocus() && !mAttachInfo.mInTouchMode) v.requestFocus();
    }

    @Override
    public boolean showContextMenuForChild(View originalView) {
        return false;
    }

    @Override
    public boolean showContextMenuForChild(View originalView, float x, float y) {
        return false;
    }

    @Override
    public void createContextMenu(ContextMenu menu) {}

    @Override
    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback) {
        return null;
    }

    @Override
    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback, int type) {
        return null;
    }

    @Override
    public void childDrawableStateChanged(View child) {}

    @Override
    public void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {}

    @Override
    public boolean requestChildRectangleOnScreen(View child, Rect rectangle, boolean immediate) {
        return false;
    }

    @Override
    public void childHasTransientStateChanged(View child, boolean hasTransientState) {}

    @Override
    public void requestFitSystemWindows() {
        if (mView != null) {
            mHandler.post(new Runnable() {
                public void run() {
                    if (mView != null) mView.dispatchApplyWindowInsets(WindowInsets.none());
                }
            });
        }
    }

    @Override
    public ViewParent getParentForAccessibility() {
        return null;
    }

    @Override
    public boolean canResolveLayoutDirection() {
        return true;
    }

    @Override
    public boolean isLayoutDirectionResolved() {
        return true;
    }

    @Override
    public int getLayoutDirection() {
        return View.LAYOUT_DIRECTION_LTR;
    }

    @Override
    public boolean canResolveTextDirection() {
        return true;
    }

    @Override
    public boolean isTextDirectionResolved() {
        return true;
    }

    @Override
    public int getTextDirection() {
        return View.TEXT_DIRECTION_FIRST_STRONG;
    }

    @Override
    public boolean canResolveTextAlignment() {
        return true;
    }

    @Override
    public boolean isTextAlignmentResolved() {
        return true;
    }

    @Override
    public int getTextAlignment() {
        return View.TEXT_ALIGNMENT_GRAVITY;
    }

    @Override
    public boolean onStartNestedScroll(View child, View target, int nestedScrollAxes) {
        return false;
    }

    @Override
    public void onNestedScrollAccepted(View child, View target, int nestedScrollAxes) {}

    @Override
    public void onStopNestedScroll(View target) {}

    @Override
    public void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed) {}

    @Override
    public void onNestedPreScroll(View target, int dx, int dy, int[] consumed) {}

    @Override
    public boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed) {
        return false;
    }

    @Override
    public boolean onNestedPreFling(View target, float velocityX, float velocityY) {
        return false;
    }

    @Override
    public boolean onNestedPrePerformAccessibilityAction(View target, int action, Bundle args) {
        return false;
    }

    // --- used by views ---------------------------------------------------------------------------------

    void invalidate() {
        scheduleTraversals();
    }

    void getWindowVisibleDisplayFrame(Rect outRect) {
        WindowManagerGlobal.getInstance().getDisplayFrame(outRect);
    }

    Rect getWindowFrame() {
        return new Rect(mWinFrame);
    }

    WindowInsetsController getInsetsController() {
        return WindowManagerGlobal.getInstance().getInsetsController();
    }

    /** Enters or leaves touch mode for all windows (keys leave it, touches enter it). */
    public boolean ensureTouchMode(boolean inTouchMode) {
        return WindowManagerGlobal.getInstance().setTouchMode(inTouchMode);
    }

    /** Called when the whole session changes touch mode. */
    void onTouchModeChanged(boolean inTouchMode) {
        if (mAttachInfo.mInTouchMode == inTouchMode) return;
        mAttachInfo.mInTouchMode = inTouchMode;
        mAttachInfo.mTreeObserver.dispatchOnTouchModeChanged(inTouchMode);
        if (mView == null) return;
        if (inTouchMode) {
            // Leaving keyboard mode: a focused view that cannot keep focus in touch mode loses it.
            View focused = mView.findFocus();
            if (focused != null && !focused.isFocusableInTouchMode()) {
                ViewGroup ancestor = findAncestorToTakeFocusInTouchMode(focused);
                if (ancestor != null) {
                    ancestor.requestFocus();
                } else {
                    focused.clearFocusInternal(true, false);
                }
            }
        } else {
            focusFirstIfNotInTouchMode();
        }
        mView.refreshDrawableStateRecursive();
        invalidate();
    }

    private static ViewGroup findAncestorToTakeFocusInTouchMode(View focused) {
        ViewParent parent = focused.getParent();
        while (parent instanceof ViewGroup) {
            final ViewGroup vgParent = (ViewGroup) parent;
            if (vgParent.getDescendantFocusability() == ViewGroup.FOCUS_AFTER_DESCENDANTS
                    && vgParent.isFocusableInTouchMode()) {
                return vgParent;
            }
            parent = vgParent.getParent();
        }
        return null;
    }

    /** In keyboard mode, a window with no focused view focuses its default or first focusable view. */
    void focusFirstIfNotInTouchMode() {
        if (mAttachInfo.mInTouchMode || mView == null || mView.hasFocus()) return;
        if (mView instanceof ViewGroup) {
            ((ViewGroup) mView).restoreDefaultFocus();
        } else {
            mView.requestFocus();
        }
    }

    // --- traversals ---------------------------------------------------------------------------------------

    /** Frames are composed for all windows at once (WindowManagerGlobal.performFrame). */
    void scheduleTraversals() {
        if (!mRemoved) WindowManagerGlobal.getInstance().scheduleFrame();
    }

    /**
     * Measures, lays out and places the window on a display of the given size.
     * Returns false if a pre-draw listener cancelled drawing this frame.
     */
    boolean performTraversals(int displayWidth, int displayHeight) {
        final View host = mView;
        if (host == null || mRemoved) return true;
        final WindowManager.LayoutParams lp = mWindowAttributes;

        if (mFirst) {
            mAttachInfo.mWindowVisibility = mStopped ? View.GONE : host.getVisibility();
            host.dispatchAttachedToWindow(mAttachInfo, 0);
            mAttachInfo.mTreeObserver.dispatchOnWindowAttachedChange(true);
            host.dispatchApplyWindowInsets(WindowInsets.none());
            mLayoutRequested = true;
        }
        final int viewVisibility = mStopped ? View.GONE : host.getVisibility();
        if (!mFirst && viewVisibility != mViewVisibility) {
            mAttachInfo.mWindowVisibility = viewVisibility;
            host.dispatchWindowVisibilityChanged(viewVisibility);
            if (viewVisibility != View.VISIBLE) host.cancelPendingInputEventsRecursive();
        }
        final boolean visibilityChanged = viewVisibility != mViewVisibility;
        mViewVisibility = viewVisibility;
        if (visibilityChanged) WindowManagerGlobal.getInstance().onWindowVisibilityChanged(this);

        final boolean displayChanged = displayWidth != mLastDisplayWidth || displayHeight != mLastDisplayHeight;
        mLastDisplayWidth = displayWidth;
        mLastDisplayHeight = displayHeight;
        boolean didLayout = false;
        if (viewVisibility != View.GONE && (mLayoutRequested || displayChanged || host.isLayoutRequested())) {
            mLayoutRequested = false;
            measureHierarchy(host, lp, displayWidth, displayHeight);
            final int w = host.getMeasuredWidth();
            final int h = host.getMeasuredHeight();
            computeFrame(lp, w, h, displayWidth, displayHeight, mWinFrame);
            mInLayout = true;
            try {
                host.layout(0, 0, w, h);
            } finally {
                mInLayout = false;
            }
            didLayout = true;
        }

        if (mFirst) {
            mFirst = false;
            if (!mAttachInfo.mInTouchMode) focusFirstIfNotInTouchMode();
        }
        if (didLayout) mAttachInfo.mTreeObserver.dispatchOnGlobalLayout();
        if (viewVisibility != View.VISIBLE) return true;
        final boolean cancelDraw = mAttachInfo.mTreeObserver.dispatchOnPreDraw();
        if (cancelDraw) {
            scheduleTraversals();
            return false;
        }
        return true;
    }

    /** AOSP measureHierarchy: wrap-content windows first try a preferred dialog width. */
    private void measureHierarchy(View host, WindowManager.LayoutParams lp, int desiredWindowWidth,
            int desiredWindowHeight) {
        boolean goodMeasure = false;
        if (lp.width == ViewGroup.LayoutParams.WRAP_CONTENT) {
            final DisplayMetrics metrics = mContext.getResources().getDisplayMetrics();
            final int baseSize = (int) (320 * metrics.density + 0.5f);
            if (baseSize != 0 && desiredWindowWidth > baseSize) {
                int childWidthMeasureSpec = getRootMeasureSpec(baseSize, lp.width);
                int childHeightMeasureSpec = getRootMeasureSpec(desiredWindowHeight, lp.height);
                host.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                if ((host.getMeasuredWidthAndState() & View.MEASURED_STATE_TOO_SMALL) == 0) {
                    goodMeasure = true;
                } else {
                    int size = (baseSize + desiredWindowWidth) / 2;
                    childWidthMeasureSpec = getRootMeasureSpec(size, lp.width);
                    host.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                    if ((host.getMeasuredWidthAndState() & View.MEASURED_STATE_TOO_SMALL) == 0) goodMeasure = true;
                }
            }
        }
        if (!goodMeasure) {
            host.measure(getRootMeasureSpec(desiredWindowWidth, lp.width),
                    getRootMeasureSpec(desiredWindowHeight, lp.height));
        }
    }

    private static int getRootMeasureSpec(int windowSize, int rootDimension) {
        switch (rootDimension) {
            case ViewGroup.LayoutParams.MATCH_PARENT:
                return View.MeasureSpec.makeMeasureSpec(windowSize, View.MeasureSpec.EXACTLY);
            case ViewGroup.LayoutParams.WRAP_CONTENT:
                return View.MeasureSpec.makeMeasureSpec(windowSize, View.MeasureSpec.AT_MOST);
            default:
                return View.MeasureSpec.makeMeasureSpec(Math.min(rootDimension, windowSize), View.MeasureSpec.EXACTLY);
        }
    }

    /** Places a w x h window on the display by its gravity, offsets and margins. */
    static void computeFrame(WindowManager.LayoutParams lp, int w, int h, int dw, int dh, Rect outFrame) {
        if (lp.width == ViewGroup.LayoutParams.MATCH_PARENT && lp.height == ViewGroup.LayoutParams.MATCH_PARENT) {
            outFrame.set(0, 0, w, h);
            return;
        }
        int gravity = lp.gravity;
        if (gravity == 0) gravity = Gravity.CENTER;
        final Rect display = new Rect(0, 0, dw, dh);
        final int xAdj = lp.x + (int) (lp.horizontalMargin * dw);
        final int yAdj = lp.y + (int) (lp.verticalMargin * dh);
        Gravity.apply(gravity, w, h, display, xAdj, yAdj, outFrame, View.LAYOUT_DIRECTION_LTR);
        Gravity.applyDisplay(gravity, display, outFrame);
    }

    /** Draws the window's views; the canvas is already translated to the window frame. */
    void draw(Canvas canvas, long drawingTime) {
        final View host = mView;
        if (host == null || !isVisibleWindow()) return;
        mAttachInfo.mDrawingTime = drawingTime;
        final int save = canvas.save();
        canvas.clipRect(0, 0, mWinFrame.width(), mWinFrame.height());
        if (lpAlpha() < 1f) {
            canvas.saveLayerAlpha(0, 0, mWinFrame.width(), mWinFrame.height(), (int) (lpAlpha() * 255));
        }
        canvas.translate(-host.getScrollX(), -host.getScrollY());
        host.computeScroll();
        if (host.skipsDraw()) {
            host.dispatchDraw(canvas);
        } else {
            host.draw(canvas);
        }
        canvas.restoreToCount(save);
        mAttachInfo.mTreeObserver.dispatchOnDraw();
    }

    private float lpAlpha() {
        return mWindowAttributes.alpha;
    }

    // --- focus and input ---------------------------------------------------------------------------------

    void windowFocusChanged(boolean hasFocus) {
        if (mHasWindowFocus == hasFocus || mView == null) return;
        mHasWindowFocus = hasFocus;
        mAttachInfo.mHasWindowFocus = hasFocus;
        mAttachInfo.mKeyDispatchState.reset();
        if (hasFocus && !mAttachInfo.mInTouchMode) focusFirstIfNotInTouchMode();
        mView.dispatchWindowFocusChanged(hasFocus);
        mAttachInfo.mTreeObserver.dispatchOnWindowFocusChange(hasFocus);
        InputMethodManager imm = InputMethodManager.peekInstance();
        if (imm != null) {
            if (hasFocus) {
                imm.onWindowFocus(mView, mView.findFocus());
            } else {
                imm.onWindowLostFocus(mView);
            }
        }
        invalidate();
    }

    boolean hasWindowFocus() {
        return mHasWindowFocus;
    }

    /** Delivers a pointer event in display coordinates. */
    boolean deliverPointerEvent(int action, float displayX, float displayY, long eventTime) {
        if (mView == null || mView.mAttachInfo == null) return false;
        if (action == MotionEvent.ACTION_DOWN) {
            mDownTime = eventTime;
            mPointerDown = true;
        }
        MotionEvent ev = MotionEvent.obtain(mDownTime, eventTime, action, displayX, displayY, 0);
        ev.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        ev.offsetLocation(-mWinFrame.left, -mWinFrame.top);
        boolean handled;
        try {
            handled = mView.dispatchPointerEvent(ev);
        } finally {
            ev.recycle();
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) mPointerDown = false;
        return handled;
    }

    boolean isPointerDown() {
        return mPointerDown;
    }

    void deliverOutsideTouch(float displayX, float displayY, long eventTime) {
        if (mView == null || mView.mAttachInfo == null) return;
        MotionEvent ev = MotionEvent.obtain(eventTime, eventTime, MotionEvent.ACTION_OUTSIDE, displayX, displayY, 0);
        ev.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        ev.offsetLocation(-mWinFrame.left, -mWinFrame.top);
        mView.dispatchPointerEvent(ev);
        ev.recycle();
    }

    boolean deliverScroll(float displayX, float displayY, float vscroll, long eventTime) {
        if (mView == null || mView.mAttachInfo == null) return false;
        MotionEvent ev = MotionEvent.obtainScroll(eventTime, displayX, displayY, vscroll, 0);
        ev.offsetLocation(-mWinFrame.left, -mWinFrame.top);
        boolean handled = mView.dispatchGenericMotionEvent(ev);
        ev.recycle();
        return handled;
    }

    /** AOSP ViewPostImeInputStage for keys: view hierarchy, then default focus navigation. */
    boolean deliverKeyEvent(KeyEvent event) {
        final View host = mView;
        if (host == null || host.mAttachInfo == null) return false;
        if (host.dispatchKeyEventPreIme(event)) return true;
        InputMethodManager imm = InputMethodManager.peekInstance();
        if (imm != null && imm.dispatchKeyEvent(this, event)) return true;
        if (host.dispatchKeyEvent(event)) return true;
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.isCtrlPressed() && event.getRepeatCount() == 0
                && !KeyEvent.isModifierKey(event.getKeyCode())) {
            if (host.dispatchKeyShortcutEvent(event)) return true;
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN) return performFocusNavigation(event);
        return false;
    }

    private boolean performFocusNavigation(KeyEvent event) {
        int direction = 0;
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_LEFT:
                if (event.hasNoModifiers()) direction = View.FOCUS_LEFT;
                break;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                if (event.hasNoModifiers()) direction = View.FOCUS_RIGHT;
                break;
            case KeyEvent.KEYCODE_DPAD_UP:
                if (event.hasNoModifiers()) direction = View.FOCUS_UP;
                break;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                if (event.hasNoModifiers()) direction = View.FOCUS_DOWN;
                break;
            case KeyEvent.KEYCODE_TAB:
                if (event.hasNoModifiers()) {
                    direction = View.FOCUS_FORWARD;
                } else if (event.hasModifiers(KeyEvent.META_SHIFT_ON)) {
                    direction = View.FOCUS_BACKWARD;
                }
                break;
        }
        if (direction == 0) return false;
        View focused = mView.findFocus();
        if (focused != null) {
            View v = focused.focusSearch(direction);
            if (v != null && v != focused) {
                Rect rect = new Rect();
                focused.getFocusedRect(rect);
                if (mView instanceof ViewGroup) {
                    ((ViewGroup) mView).offsetDescendantRectToMyCoords(focused, rect);
                    ((ViewGroup) mView).offsetRectIntoDescendantCoords(v, rect);
                }
                if (v.requestFocus(direction, rect)) return true;
            }
        } else if (mView instanceof ViewGroup ? ((ViewGroup) mView).restoreDefaultFocus() : mView.requestFocus()) {
            return true;
        }
        return false;
    }

    @Override
    public String toString() {
        return "ViewRootImpl{" + Integer.toHexString(System.identityHashCode(this)) + " " + mTitle + "}";
    }

    static void logw(String msg) {
        Log.w(TAG, msg);
    }

    // --- host callbacks (invoked by the main looper's native poll) ---------------------------------

    static void dispatchPointer(int action, float x, float y, long eventTime) {
        WindowManagerGlobal.getInstance().dispatchPointer(action, x, y, eventTime);
    }

    static void dispatchResize(int width, int height) {
        WindowManagerGlobal.getInstance().dispatchResize(width, height);
    }

    static void dispatchCloseRequest() {
        WindowManagerGlobal.getInstance().dispatchCloseRequest();
    }

    static void dispatchBack() {
        WindowManagerGlobal.getInstance().dispatchBack();
    }

    static void dispatchKey(int action, int keyCode, int unicode, int metaState, long eventTime) {
        WindowManagerGlobal.getInstance().dispatchKey(action, keyCode, unicode, metaState, eventTime);
    }

    static void dispatchScroll(float x, float y, float vscroll, long eventTime) {
        WindowManagerGlobal.getInstance().dispatchScroll(x, y, vscroll, eventTime);
    }

    /** Creates the host window; out = {width, height} in pixels. Returns the surface handle. */
    static native long nativeOpenWindow(String title, int[] outSize);

    static native void nativeSetTitle(long surface, String title);

    static native void nativePresent(long surface);

    static native void nativeCloseWindow(long surface);
}
