package android.view;

import android.graphics.Point;
import android.graphics.Rect;

/** The contract of a view's parent: a ViewGroup, or a ViewRootImpl at the top. */
public interface ViewParent {
    void requestLayout();

    boolean isLayoutRequested();

    void requestTransparentRegion(View child);

    @Deprecated
    void invalidateChild(View child, Rect r);

    @Deprecated
    ViewParent invalidateChildInParent(int[] location, Rect r);

    ViewParent getParent();

    void requestChildFocus(View child, View focused);

    void recomputeViewAttributes(View child);

    void clearChildFocus(View child);

    boolean getChildVisibleRect(View child, Rect r, Point offset);

    View focusSearch(View v, int direction);

    View keyboardNavigationClusterSearch(View currentCluster, int direction);

    void bringChildToFront(View child);

    void focusableViewAvailable(View v);

    boolean showContextMenuForChild(View originalView);

    boolean showContextMenuForChild(View originalView, float x, float y);

    void createContextMenu(ContextMenu menu);

    ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback);

    ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback, int type);

    void childDrawableStateChanged(View child);

    void requestDisallowInterceptTouchEvent(boolean disallowIntercept);

    boolean requestChildRectangleOnScreen(View child, Rect rectangle, boolean immediate);

    void childHasTransientStateChanged(View child, boolean hasTransientState);

    void requestFitSystemWindows();

    ViewParent getParentForAccessibility();

    boolean canResolveLayoutDirection();

    boolean isLayoutDirectionResolved();

    int getLayoutDirection();

    boolean canResolveTextDirection();

    boolean isTextDirectionResolved();

    int getTextDirection();

    boolean canResolveTextAlignment();

    boolean isTextAlignmentResolved();

    int getTextAlignment();

    boolean onStartNestedScroll(View child, View target, int nestedScrollAxes);

    void onNestedScrollAccepted(View child, View target, int nestedScrollAxes);

    void onStopNestedScroll(View target);

    void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed);

    void onNestedPreScroll(View target, int dx, int dy, int[] consumed);

    boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed);

    boolean onNestedPreFling(View target, float velocityX, float velocityY);

    boolean onNestedPrePerformAccessibilityAction(View target, int action, android.os.Bundle arguments);
}
