package android.view;

import java.util.ArrayList;

/** Global view-tree notifications (layout, pre-draw, draw, focus, scroll, window focus). */
public final class ViewTreeObserver {
    public interface OnGlobalFocusChangeListener {
        void onGlobalFocusChanged(View oldFocus, View newFocus);
    }

    public interface OnGlobalLayoutListener {
        void onGlobalLayout();
    }

    public interface OnPreDrawListener {
        boolean onPreDraw();
    }

    public interface OnDrawListener {
        void onDraw();
    }

    public interface OnTouchModeChangeListener {
        void onTouchModeChanged(boolean isInTouchMode);
    }

    public interface OnScrollChangedListener {
        void onScrollChanged();
    }

    public interface OnWindowFocusChangeListener {
        void onWindowFocusChanged(boolean hasFocus);
    }

    public interface OnWindowAttachListener {
        void onWindowAttached();

        void onWindowDetached();
    }

    private ArrayList<OnGlobalFocusChangeListener> mOnGlobalFocusListeners;
    private ArrayList<OnGlobalLayoutListener> mOnGlobalLayoutListeners;
    private ArrayList<OnPreDrawListener> mOnPreDrawListeners;
    private ArrayList<OnDrawListener> mOnDrawListeners;
    private ArrayList<OnTouchModeChangeListener> mOnTouchModeChangeListeners;
    private ArrayList<OnScrollChangedListener> mOnScrollChangedListeners;
    private ArrayList<OnWindowFocusChangeListener> mOnWindowFocusListeners;
    private ArrayList<OnWindowAttachListener> mOnWindowAttachListeners;
    private boolean mAlive = true;

    ViewTreeObserver() {}

    /** Moves the listeners of a detached view's observer into this one. */
    void merge(ViewTreeObserver observer) {
        mOnGlobalFocusListeners = mergeList(mOnGlobalFocusListeners, observer.mOnGlobalFocusListeners);
        mOnGlobalLayoutListeners = mergeList(mOnGlobalLayoutListeners, observer.mOnGlobalLayoutListeners);
        mOnPreDrawListeners = mergeList(mOnPreDrawListeners, observer.mOnPreDrawListeners);
        mOnDrawListeners = mergeList(mOnDrawListeners, observer.mOnDrawListeners);
        mOnTouchModeChangeListeners = mergeList(mOnTouchModeChangeListeners, observer.mOnTouchModeChangeListeners);
        mOnScrollChangedListeners = mergeList(mOnScrollChangedListeners, observer.mOnScrollChangedListeners);
        mOnWindowFocusListeners = mergeList(mOnWindowFocusListeners, observer.mOnWindowFocusListeners);
        mOnWindowAttachListeners = mergeList(mOnWindowAttachListeners, observer.mOnWindowAttachListeners);
        observer.kill();
    }

    private static <T> ArrayList<T> mergeList(ArrayList<T> into, ArrayList<T> from) {
        if (from == null || from.isEmpty()) return into;
        if (into == null) return new ArrayList<T>(from);
        into.addAll(from);
        return into;
    }

    private void checkIsAlive() {
        if (!mAlive) {
            throw new IllegalStateException("This ViewTreeObserver is not alive, call getViewTreeObserver() again");
        }
    }

    public boolean isAlive() {
        return mAlive;
    }

    private void kill() {
        mAlive = false;
    }

    private static <T> ArrayList<T> add(ArrayList<T> list, T listener) {
        if (listener == null) throw new IllegalArgumentException("listener is null");
        if (list == null) list = new ArrayList<T>();
        list.add(listener);
        return list;
    }

    public void addOnGlobalFocusChangeListener(OnGlobalFocusChangeListener listener) {
        checkIsAlive();
        mOnGlobalFocusListeners = add(mOnGlobalFocusListeners, listener);
    }

    public void removeOnGlobalFocusChangeListener(OnGlobalFocusChangeListener victim) {
        checkIsAlive();
        if (mOnGlobalFocusListeners != null) mOnGlobalFocusListeners.remove(victim);
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener listener) {
        checkIsAlive();
        mOnGlobalLayoutListeners = add(mOnGlobalLayoutListeners, listener);
    }

    @Deprecated
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener victim) {
        removeOnGlobalLayoutListener(victim);
    }

    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener victim) {
        checkIsAlive();
        if (mOnGlobalLayoutListeners != null) mOnGlobalLayoutListeners.remove(victim);
    }

    public void addOnPreDrawListener(OnPreDrawListener listener) {
        checkIsAlive();
        mOnPreDrawListeners = add(mOnPreDrawListeners, listener);
    }

    public void removeOnPreDrawListener(OnPreDrawListener victim) {
        checkIsAlive();
        if (mOnPreDrawListeners != null) mOnPreDrawListeners.remove(victim);
    }

    public void addOnDrawListener(OnDrawListener listener) {
        checkIsAlive();
        mOnDrawListeners = add(mOnDrawListeners, listener);
    }

    public void removeOnDrawListener(OnDrawListener victim) {
        checkIsAlive();
        if (mOnDrawListeners != null) mOnDrawListeners.remove(victim);
    }

    public void addOnTouchModeChangeListener(OnTouchModeChangeListener listener) {
        checkIsAlive();
        mOnTouchModeChangeListeners = add(mOnTouchModeChangeListeners, listener);
    }

    public void removeOnTouchModeChangeListener(OnTouchModeChangeListener victim) {
        checkIsAlive();
        if (mOnTouchModeChangeListeners != null) mOnTouchModeChangeListeners.remove(victim);
    }

    public void addOnScrollChangedListener(OnScrollChangedListener listener) {
        checkIsAlive();
        mOnScrollChangedListeners = add(mOnScrollChangedListeners, listener);
    }

    public void removeOnScrollChangedListener(OnScrollChangedListener victim) {
        checkIsAlive();
        if (mOnScrollChangedListeners != null) mOnScrollChangedListeners.remove(victim);
    }

    public void addOnWindowFocusChangeListener(OnWindowFocusChangeListener listener) {
        checkIsAlive();
        mOnWindowFocusListeners = add(mOnWindowFocusListeners, listener);
    }

    public void removeOnWindowFocusChangeListener(OnWindowFocusChangeListener victim) {
        checkIsAlive();
        if (mOnWindowFocusListeners != null) mOnWindowFocusListeners.remove(victim);
    }

    public void addOnWindowAttachListener(OnWindowAttachListener listener) {
        checkIsAlive();
        mOnWindowAttachListeners = add(mOnWindowAttachListeners, listener);
    }

    public void removeOnWindowAttachListener(OnWindowAttachListener victim) {
        checkIsAlive();
        if (mOnWindowAttachListeners != null) mOnWindowAttachListeners.remove(victim);
    }

    final void dispatchOnGlobalFocusChange(View oldFocus, View newFocus) {
        if (mOnGlobalFocusListeners == null) return;
        for (OnGlobalFocusChangeListener l : new ArrayList<OnGlobalFocusChangeListener>(mOnGlobalFocusListeners)) {
            l.onGlobalFocusChanged(oldFocus, newFocus);
        }
    }

    public final void dispatchOnGlobalLayout() {
        if (mOnGlobalLayoutListeners == null) return;
        for (OnGlobalLayoutListener l : new ArrayList<OnGlobalLayoutListener>(mOnGlobalLayoutListeners)) {
            l.onGlobalLayout();
        }
    }

    /** Returns true when a listener asked to cancel the current draw. */
    public final boolean dispatchOnPreDraw() {
        boolean cancelDraw = false;
        if (mOnPreDrawListeners != null) {
            for (OnPreDrawListener l : new ArrayList<OnPreDrawListener>(mOnPreDrawListeners)) {
                cancelDraw |= !l.onPreDraw();
            }
        }
        return cancelDraw;
    }

    public final void dispatchOnDraw() {
        if (mOnDrawListeners == null) return;
        for (OnDrawListener l : new ArrayList<OnDrawListener>(mOnDrawListeners)) l.onDraw();
    }

    final void dispatchOnTouchModeChanged(boolean inTouchMode) {
        if (mOnTouchModeChangeListeners == null) return;
        for (OnTouchModeChangeListener l : new ArrayList<OnTouchModeChangeListener>(mOnTouchModeChangeListeners)) {
            l.onTouchModeChanged(inTouchMode);
        }
    }

    final void dispatchOnScrollChanged() {
        if (mOnScrollChangedListeners == null) return;
        for (OnScrollChangedListener l : new ArrayList<OnScrollChangedListener>(mOnScrollChangedListeners)) {
            l.onScrollChanged();
        }
    }

    final void dispatchOnWindowFocusChange(boolean hasFocus) {
        if (mOnWindowFocusListeners == null) return;
        for (OnWindowFocusChangeListener l : new ArrayList<OnWindowFocusChangeListener>(mOnWindowFocusListeners)) {
            l.onWindowFocusChanged(hasFocus);
        }
    }

    final void dispatchOnWindowAttachedChange(boolean attached) {
        if (mOnWindowAttachListeners == null) return;
        for (OnWindowAttachListener l : new ArrayList<OnWindowAttachListener>(mOnWindowAttachListeners)) {
            if (attached) l.onWindowAttached(); else l.onWindowDetached();
        }
    }
}
