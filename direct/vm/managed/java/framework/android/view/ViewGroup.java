package android.view;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.Parcelable;
import android.util.AttributeSet;
import android.util.SparseArray;
import android.view.animation.Transformation;
import java.util.ArrayList;

/**
 * A view that contains other views: child management, layout parameters,
 * touch and key dispatch, focus, and drawing of children (with transforms,
 * alpha and view animations), following Android's ViewGroup contracts.
 */
public abstract class ViewGroup extends View implements ViewParent, ViewManager {
    public static final int FOCUS_BEFORE_DESCENDANTS = 0x20000;
    public static final int FOCUS_AFTER_DESCENDANTS = 0x40000;
    public static final int FOCUS_BLOCK_DESCENDANTS = 0x60000;
    private static final int FLAG_MASK_FOCUSABILITY = 0x60000;

    public static final int PERSISTENT_NO_CACHE = 0x0;
    public static final int PERSISTENT_ANIMATION_CACHE = 0x1;
    public static final int PERSISTENT_SCROLLING_CACHE = 0x2;
    public static final int PERSISTENT_ALL_CACHES = 0x3;

    public static final int LAYOUT_MODE_CLIP_BOUNDS = 0;
    public static final int LAYOUT_MODE_OPTICAL_BOUNDS = 1;

    private static final int[] DESCENDANT_FOCUSABILITY_FLAGS = {FOCUS_BEFORE_DESCENDANTS, FOCUS_AFTER_DESCENDANTS,
            FOCUS_BLOCK_DESCENDANTS};

    public interface OnHierarchyChangeListener {
        void onChildViewAdded(View parent, View child);

        void onChildViewRemoved(View parent, View child);
    }

    public static class LayoutParams {
        @Deprecated
        public static final int FILL_PARENT = -1;
        public static final int MATCH_PARENT = -1;
        public static final int WRAP_CONTENT = -2;

        public int width;
        public int height;

        public LayoutParams(Context c, AttributeSet attrs) {
            TypedArray a = c.obtainStyledAttributes(attrs, android.R.styleable.ViewGroup_Layout);
            setBaseAttributes(a, android.R.styleable.ViewGroup_Layout_layout_width,
                    android.R.styleable.ViewGroup_Layout_layout_height);
            a.recycle();
        }

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }

        public LayoutParams(LayoutParams source) {
            this.width = source.width;
            this.height = source.height;
        }

        LayoutParams() {}

        protected void setBaseAttributes(TypedArray a, int widthAttr, int heightAttr) {
            width = a.getLayoutDimension(widthAttr, "layout_width");
            height = a.getLayoutDimension(heightAttr, "layout_height");
        }

        public void resolveLayoutDirection(int layoutDirection) {}

        protected static String sizeToString(int size) {
            if (size == WRAP_CONTENT) return "wrap-content";
            if (size == MATCH_PARENT) return "match-parent";
            return String.valueOf(size);
        }

        @Override
        public String toString() {
            return getClass().getSimpleName() + "{ width=" + sizeToString(width) + ", height=" + sizeToString(height)
                    + " }";
        }
    }

    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin;
        public int topMargin;
        public int rightMargin;
        public int bottomMargin;
        private int startMargin = DEFAULT_MARGIN_RELATIVE;
        private int endMargin = DEFAULT_MARGIN_RELATIVE;
        static final int DEFAULT_MARGIN_RELATIVE = Integer.MIN_VALUE;

        public MarginLayoutParams(Context c, AttributeSet attrs) {
            super();
            TypedArray a = c.obtainStyledAttributes(attrs, android.R.styleable.ViewGroup_MarginLayout);
            setBaseAttributes(a, android.R.styleable.ViewGroup_MarginLayout_layout_width,
                    android.R.styleable.ViewGroup_MarginLayout_layout_height);
            int margin = a.getDimensionPixelSize(android.R.styleable.ViewGroup_MarginLayout_layout_margin, -1);
            if (margin >= 0) {
                leftMargin = topMargin = rightMargin = bottomMargin = margin;
            } else {
                int horizontal = a.getDimensionPixelSize(
                        android.R.styleable.ViewGroup_MarginLayout_layout_marginHorizontal, -1);
                int vertical = a.getDimensionPixelSize(
                        android.R.styleable.ViewGroup_MarginLayout_layout_marginVertical, -1);
                if (horizontal >= 0) {
                    leftMargin = rightMargin = horizontal;
                } else {
                    leftMargin = a.getDimensionPixelSize(android.R.styleable.ViewGroup_MarginLayout_layout_marginLeft,
                            0);
                    rightMargin = a.getDimensionPixelSize(
                            android.R.styleable.ViewGroup_MarginLayout_layout_marginRight, 0);
                    startMargin = a.getDimensionPixelSize(
                            android.R.styleable.ViewGroup_MarginLayout_layout_marginStart, DEFAULT_MARGIN_RELATIVE);
                    endMargin = a.getDimensionPixelSize(android.R.styleable.ViewGroup_MarginLayout_layout_marginEnd,
                            DEFAULT_MARGIN_RELATIVE);
                }
                if (vertical >= 0) {
                    topMargin = bottomMargin = vertical;
                } else {
                    topMargin = a.getDimensionPixelSize(android.R.styleable.ViewGroup_MarginLayout_layout_marginTop,
                            0);
                    bottomMargin = a.getDimensionPixelSize(
                            android.R.styleable.ViewGroup_MarginLayout_layout_marginBottom, 0);
                }
            }
            a.recycle();
            resolveLayoutDirection(LAYOUT_DIRECTION_LTR);
        }

        public MarginLayoutParams(int width, int height) {
            super(width, height);
        }

        public MarginLayoutParams(MarginLayoutParams source) {
            this.width = source.width;
            this.height = source.height;
            this.leftMargin = source.leftMargin;
            this.topMargin = source.topMargin;
            this.rightMargin = source.rightMargin;
            this.bottomMargin = source.bottomMargin;
            this.startMargin = source.startMargin;
            this.endMargin = source.endMargin;
        }

        public MarginLayoutParams(LayoutParams source) {
            super(source);
        }

        public final void copyMarginsFrom(MarginLayoutParams source) {
            this.leftMargin = source.leftMargin;
            this.topMargin = source.topMargin;
            this.rightMargin = source.rightMargin;
            this.bottomMargin = source.bottomMargin;
            this.startMargin = source.startMargin;
            this.endMargin = source.endMargin;
        }

        public void setMargins(int left, int top, int right, int bottom) {
            leftMargin = left;
            topMargin = top;
            rightMargin = right;
            bottomMargin = bottom;
            startMargin = DEFAULT_MARGIN_RELATIVE;
            endMargin = DEFAULT_MARGIN_RELATIVE;
        }

        public void setMarginsRelative(int start, int top, int end, int bottom) {
            startMargin = start;
            topMargin = top;
            endMargin = end;
            bottomMargin = bottom;
            resolveLayoutDirection(LAYOUT_DIRECTION_LTR);
        }

        public void setMarginStart(int start) {
            startMargin = start;
            resolveLayoutDirection(LAYOUT_DIRECTION_LTR);
        }

        public int getMarginStart() {
            return startMargin != DEFAULT_MARGIN_RELATIVE ? startMargin : leftMargin;
        }

        public void setMarginEnd(int end) {
            endMargin = end;
            resolveLayoutDirection(LAYOUT_DIRECTION_LTR);
        }

        public int getMarginEnd() {
            return endMargin != DEFAULT_MARGIN_RELATIVE ? endMargin : rightMargin;
        }

        public boolean isMarginRelative() {
            return startMargin != DEFAULT_MARGIN_RELATIVE || endMargin != DEFAULT_MARGIN_RELATIVE;
        }

        public int getLayoutDirection() {
            return LAYOUT_DIRECTION_LTR;
        }

        public void setLayoutDirection(int layoutDirection) {}

        /** Layout is left-to-right only: start margins resolve to left, end to right. */
        @Override
        public void resolveLayoutDirection(int layoutDirection) {
            if (startMargin != DEFAULT_MARGIN_RELATIVE) leftMargin = startMargin;
            if (endMargin != DEFAULT_MARGIN_RELATIVE) rightMargin = endMargin;
        }
    }

    private final ArrayList<View> mChildren = new ArrayList<View>();
    private View mFocusedChild;
    private View mFirstTouchTarget;
    private View mHoveredChild;
    private int mDescendantFocusability = FOCUS_BEFORE_DESCENDANTS;
    private boolean mDisallowIntercept;
    private boolean mClipChildren = true;
    private boolean mClipToPadding = true;
    private boolean mAddStatesFromChildren;
    private boolean mChildrenDrawingOrderEnabled;
    private boolean mStaticTransformationsEnabled;
    private boolean mSplitMotionEvents = true;
    private boolean mTouchscreenBlocksFocus;
    private boolean mSuppressLayout;
    private boolean mLayoutCalledWhileSuppressed;
    private int mPersistentDrawingCache = PERSISTENT_SCROLLING_CACHE;
    private int mLayoutMode = LAYOUT_MODE_CLIP_BOUNDS;
    private OnHierarchyChangeListener mOnHierarchyChangeListener;
    private ArrayList<View> mTransitioningViews;
    private final Transformation mChildTransformation = new Transformation();

    public ViewGroup(Context context) {
        super(context);
        initViewGroup();
    }

    public ViewGroup(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public ViewGroup(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public ViewGroup(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        initViewGroup();
        final TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.ViewGroup, defStyleAttr,
                defStyleRes);
        final int n = a.getIndexCount();
        for (int i = 0; i < n; i++) {
            int attr = a.getIndex(i);
            switch (attr) {
                case android.R.styleable.ViewGroup_clipChildren:
                    setClipChildren(a.getBoolean(attr, true));
                    break;
                case android.R.styleable.ViewGroup_clipToPadding:
                    setClipToPadding(a.getBoolean(attr, true));
                    break;
                case android.R.styleable.ViewGroup_addStatesFromChildren:
                    setAddStatesFromChildren(a.getBoolean(attr, false));
                    break;
                case android.R.styleable.ViewGroup_descendantFocusability:
                    setDescendantFocusability(DESCENDANT_FOCUSABILITY_FLAGS[a.getInt(attr, 0)]);
                    break;
                case android.R.styleable.ViewGroup_splitMotionEvents:
                    setMotionEventSplittingEnabled(a.getBoolean(attr, false));
                    break;
                case android.R.styleable.ViewGroup_touchscreenBlocksFocus:
                    setTouchscreenBlocksFocus(a.getBoolean(attr, false));
                    break;
                default:
                    break;
            }
        }
        a.recycle();
    }

    private void initViewGroup() {
        setWillNotDraw(true);
    }

    // --- children ---------------------------------------------------------------------

    public int getChildCount() {
        return mChildren.size();
    }

    public View getChildAt(int index) {
        if (index < 0 || index >= mChildren.size()) return null;
        return mChildren.get(index);
    }

    public int indexOfChild(View child) {
        return mChildren.indexOf(child);
    }

    public void addView(View child) {
        addView(child, -1);
    }

    public void addView(View child, int index) {
        if (child == null) throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        LayoutParams params = child.getLayoutParams();
        if (params == null) {
            params = generateDefaultLayoutParams();
            if (params == null) {
                throw new IllegalArgumentException("generateDefaultLayoutParams() cannot return null");
            }
        }
        addView(child, index, params);
    }

    public void addView(View child, int width, int height) {
        final LayoutParams params = generateDefaultLayoutParams();
        params.width = width;
        params.height = height;
        addView(child, -1, params);
    }

    @Override
    public void addView(View child, LayoutParams params) {
        addView(child, -1, params);
    }

    public void addView(View child, int index, LayoutParams params) {
        if (child == null) throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        requestLayout();
        invalidate();
        addViewInner(child, index, params, false);
    }

    @Override
    public void updateViewLayout(View view, LayoutParams params) {
        if (!checkLayoutParams(params)) {
            throw new IllegalArgumentException("Invalid LayoutParams supplied to " + this);
        }
        if (view.mParent != this) throw new IllegalArgumentException("Given view not a child of " + this);
        view.setLayoutParams(params);
    }

    protected boolean addViewInLayout(View child, int index, LayoutParams params) {
        return addViewInLayout(child, index, params, false);
    }

    protected boolean addViewInLayout(View child, int index, LayoutParams params, boolean preventRequestLayout) {
        if (child == null) throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        child.mParent = null;
        addViewInner(child, index, params, preventRequestLayout);
        return true;
    }

    protected void cleanupLayoutState(View child) {
        child.mForceLayout = false;
    }

    private void addViewInner(View child, int index, LayoutParams params, boolean preventRequestLayout) {
        if (child.getParent() != null) {
            throw new IllegalStateException("The specified child already has a parent. "
                    + "You must call removeView() on the child's parent first.");
        }
        if (!checkLayoutParams(params)) params = generateLayoutParams(params);
        if (preventRequestLayout) {
            child.mLayoutParams = params;
        } else {
            child.setLayoutParams(params);
        }
        if (index < 0) index = mChildren.size();
        mChildren.add(index, child);
        child.mParent = this;
        if (child.hasFocus()) requestChildFocus(child, child.findFocus());
        if (mAttachInfo != null) child.dispatchAttachedToWindow(mAttachInfo, getVisibility() | child.getVisibility());
        onViewAdded(child);
        if (mOnHierarchyChangeListener != null) mOnHierarchyChangeListener.onChildViewAdded(this, child);
        if (child.isDuplicateParentStateEnabled()) child.refreshDrawableState();
    }

    public void onViewAdded(View child) {}

    public void onViewRemoved(View child) {}

    @Override
    public void removeView(View view) {
        if (removeViewInternal(view)) {
            requestLayout();
            invalidate();
        }
    }

    public void removeViewInLayout(View view) {
        removeViewInternal(view);
    }

    public void removeViewsInLayout(int start, int count) {
        removeViewsInternal(start, count);
    }

    public void removeViewAt(int index) {
        removeViewInternal(index, getChildAt(index));
        requestLayout();
        invalidate();
    }

    public void removeViews(int start, int count) {
        removeViewsInternal(start, count);
        requestLayout();
        invalidate();
    }

    public void removeAllViews() {
        removeAllViewsInLayout();
        requestLayout();
        invalidate();
    }

    public void removeAllViewsInLayout() {
        removeViewsInternal(0, mChildren.size());
    }

    private boolean removeViewInternal(View view) {
        final int index = mChildren.indexOf(view);
        if (index >= 0) {
            removeViewInternal(index, view);
            return true;
        }
        return false;
    }

    private void removeViewInternal(int index, View view) {
        boolean clearChildFocus = false;
        if (view == mFocusedChild) {
            view.unFocus(null);
            clearChildFocus = true;
        }
        if (view == mFirstTouchTarget) mFirstTouchTarget = null;
        if (view == mHoveredChild) mHoveredChild = null;
        if (view.mAttachInfo != null) view.dispatchDetachedFromWindow();
        mChildren.remove(index);
        view.mParent = null;
        onViewRemoved(view);
        if (mOnHierarchyChangeListener != null) mOnHierarchyChangeListener.onChildViewRemoved(this, view);
        if (clearChildFocus) {
            clearChildFocus(view);
            if (mAttachInfo != null) mAttachInfo.mViewRootImpl.focusFirstIfNotInTouchMode();
        }
    }

    private void removeViewsInternal(int start, int count) {
        final int end = Math.min(mChildren.size(), start + count);
        for (int i = end - 1; i >= start; i--) removeViewInternal(i, mChildren.get(i));
    }

    protected void attachViewToParent(View child, int index, LayoutParams params) {
        child.mLayoutParams = params;
        if (index < 0) index = mChildren.size();
        mChildren.add(index, child);
        child.mParent = this;
        if (child.hasFocus()) requestChildFocus(child, child.findFocus());
    }

    protected void detachViewFromParent(View child) {
        detachViewFromParent(mChildren.indexOf(child));
    }

    protected void detachViewFromParent(int index) {
        if (index < 0 || index >= mChildren.size()) return;
        View child = mChildren.remove(index);
        if (child == mFirstTouchTarget) mFirstTouchTarget = null;
        child.mParent = null;
    }

    protected void detachViewsFromParent(int start, int count) {
        final int end = Math.min(mChildren.size(), start + count);
        for (int i = end - 1; i >= start; i--) detachViewFromParent(i);
    }

    protected void detachAllViewsFromParent() {
        detachViewsFromParent(0, mChildren.size());
    }

    protected void removeDetachedView(View child, boolean animate) {
        if (child == mFocusedChild) child.clearFocus();
        if (child.mAttachInfo != null) child.dispatchDetachedFromWindow();
        if (mOnHierarchyChangeListener != null) mOnHierarchyChangeListener.onChildViewRemoved(this, child);
    }

    public void setOnHierarchyChangeListener(OnHierarchyChangeListener listener) {
        mOnHierarchyChangeListener = listener;
    }

    public void bringChildToFront(View child) {
        final int index = mChildren.indexOf(child);
        if (index >= 0) {
            mChildren.remove(index);
            mChildren.add(child);
            requestLayout();
            invalidate();
        }
    }

    public void startViewTransition(View view) {
        if (view.mParent == this) {
            if (mTransitioningViews == null) mTransitioningViews = new ArrayList<View>();
            mTransitioningViews.add(view);
        }
    }

    public void endViewTransition(View view) {
        if (mTransitioningViews != null) mTransitioningViews.remove(view);
    }

    public void suppressLayout(boolean suppress) {
        mSuppressLayout = suppress;
        if (!suppress && mLayoutCalledWhileSuppressed) {
            requestLayout();
            mLayoutCalledWhileSuppressed = false;
        }
    }

    public boolean isLayoutSuppressed() {
        return mSuppressLayout;
    }

    @Override
    public final void layout(int l, int t, int r, int b) {
        if (!mSuppressLayout) {
            super.layout(l, t, r, b);
        } else {
            mLayoutCalledWhileSuppressed = true;
        }
    }

    @Override
    protected abstract void onLayout(boolean changed, int l, int t, int r, int b);

    // --- attach / detach and visibility -------------------------------------------------

    @Override
    void dispatchAttachedToWindow(AttachInfo info, int visibility) {
        super.dispatchAttachedToWindow(info, visibility);
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            child.dispatchAttachedToWindow(info, combineVisibility(visibility, child.getVisibility()));
        }
    }

    @Override
    void dispatchDetachedFromWindow() {
        mFirstTouchTarget = null;
        mHoveredChild = null;
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).dispatchDetachedFromWindow();
        super.dispatchDetachedFromWindow();
    }

    static int combineVisibility(int vis1, int vis2) {
        return Math.max(vis1, vis2);
    }

    @Override
    protected void dispatchVisibilityChanged(View changedView, int visibility) {
        super.dispatchVisibilityChanged(changedView, visibility);
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).dispatchVisibilityChanged(changedView, visibility);
    }

    @Override
    public void dispatchWindowVisibilityChanged(int visibility) {
        super.dispatchWindowVisibilityChanged(visibility);
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).dispatchWindowVisibilityChanged(visibility);
    }

    @Override
    public void dispatchWindowFocusChanged(boolean hasFocus) {
        super.dispatchWindowFocusChanged(hasFocus);
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).dispatchWindowFocusChanged(hasFocus);
    }

    protected void onChildVisibilityChanged(View child, int oldVisibility, int newVisibility) {
        if (newVisibility != VISIBLE && child == mFocusedChild) child.clearFocus();
    }

    @Override
    public WindowInsets dispatchApplyWindowInsets(WindowInsets insets) {
        insets = super.dispatchApplyWindowInsets(insets);
        if (!insets.isConsumed()) {
            for (int i = 0; i < mChildren.size(); i++) {
                insets = mChildren.get(i).dispatchApplyWindowInsets(insets);
                if (insets.isConsumed()) break;
            }
        }
        return insets;
    }

    // --- lookup ---------------------------------------------------------------------------

    @Override
    protected <T extends View> T findViewTraversal(int id) {
        T v = super.findViewTraversal(id);
        if (v != null) return v;
        for (int i = 0; i < mChildren.size(); i++) {
            v = mChildren.get(i).findViewTraversal(id);
            if (v != null) return v;
        }
        return null;
    }

    @Override
    protected <T extends View> T findViewWithTagTraversal(Object tag) {
        T v = super.findViewWithTagTraversal(tag);
        if (v != null) return v;
        for (int i = 0; i < mChildren.size(); i++) {
            v = mChildren.get(i).findViewWithTagTraversal(tag);
            if (v != null) return v;
        }
        return null;
    }

    @Override
    public void findViewsWithText(ArrayList<View> outViews, CharSequence text, int flags) {
        super.findViewsWithText(outViews, text, flags);
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            if (child.getVisibility() == VISIBLE) child.findViewsWithText(outViews, text, flags);
        }
    }

    // --- layout params ---------------------------------------------------------------------

    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    }

    protected LayoutParams generateLayoutParams(LayoutParams p) {
        return p;
    }

    public LayoutParams generateLayoutParams(AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    protected boolean checkLayoutParams(LayoutParams p) {
        return p != null;
    }

    void onSetLayoutParams(View child, LayoutParams params) {
        requestLayout();
    }

    public int getLayoutMode() {
        return mLayoutMode;
    }

    public void setLayoutMode(int layoutMode) {
        mLayoutMode = layoutMode;
    }

    // --- measurement helpers ----------------------------------------------------------------

    protected void measureChildren(int widthMeasureSpec, int heightMeasureSpec) {
        for (int i = 0; i < mChildren.size(); ++i) {
            final View child = mChildren.get(i);
            if (child.getVisibility() != GONE) measureChild(child, widthMeasureSpec, heightMeasureSpec);
        }
    }

    protected void measureChild(View child, int parentWidthMeasureSpec, int parentHeightMeasureSpec) {
        final LayoutParams lp = child.getLayoutParams();
        final int childWidthMeasureSpec = getChildMeasureSpec(parentWidthMeasureSpec,
                mPaddingLeft + mPaddingRight, lp.width);
        final int childHeightMeasureSpec = getChildMeasureSpec(parentHeightMeasureSpec,
                mPaddingTop + mPaddingBottom, lp.height);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    protected void measureChildWithMargins(View child, int parentWidthMeasureSpec, int widthUsed,
            int parentHeightMeasureSpec, int heightUsed) {
        final MarginLayoutParams lp = (MarginLayoutParams) child.getLayoutParams();
        final int childWidthMeasureSpec = getChildMeasureSpec(parentWidthMeasureSpec,
                mPaddingLeft + mPaddingRight + lp.leftMargin + lp.rightMargin + widthUsed, lp.width);
        final int childHeightMeasureSpec = getChildMeasureSpec(parentHeightMeasureSpec,
                mPaddingTop + mPaddingBottom + lp.topMargin + lp.bottomMargin + heightUsed, lp.height);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    public static int getChildMeasureSpec(int spec, int padding, int childDimension) {
        int specMode = MeasureSpec.getMode(spec);
        int specSize = MeasureSpec.getSize(spec);
        int size = Math.max(0, specSize - padding);
        int resultSize = 0;
        int resultMode = 0;
        switch (specMode) {
            case MeasureSpec.EXACTLY:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.MATCH_PARENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.WRAP_CONTENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.AT_MOST;
                }
                break;
            case MeasureSpec.AT_MOST:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.MATCH_PARENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.AT_MOST;
                } else if (childDimension == LayoutParams.WRAP_CONTENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.AT_MOST;
                }
                break;
            case MeasureSpec.UNSPECIFIED:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else {
                    resultSize = 0;
                    resultMode = MeasureSpec.UNSPECIFIED;
                }
                break;
        }
        return MeasureSpec.makeMeasureSpec(resultSize, resultMode);
    }

    // --- ViewParent ----------------------------------------------------------------------------

    @Override
    public void requestTransparentRegion(View child) {}

    @Override
    public final void invalidateChild(View child, Rect dirty) {
        invalidate();
    }

    @Override
    public ViewParent invalidateChildInParent(int[] location, Rect dirty) {
        return mParent;
    }

    @Override
    public void recomputeViewAttributes(View child) {
        if (mParent != null) mParent.recomputeViewAttributes(this);
    }

    @Override
    public void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {
        if (disallowIntercept == mDisallowIntercept) return;
        mDisallowIntercept = disallowIntercept;
        if (mParent != null) mParent.requestDisallowInterceptTouchEvent(disallowIntercept);
    }

    @Override
    public boolean getChildVisibleRect(View child, Rect r, Point offset) {
        final RectF rect = new RectF(r);
        if (!child.hasIdentityMatrix()) child.getMatrix().mapRect(rect);
        final int dx = child.mLeft - mScrollX;
        final int dy = child.mTop - mScrollY;
        rect.offset(dx, dy);
        if (offset != null) {
            if (!child.hasIdentityMatrix()) {
                float[] position = {offset.x, offset.y};
                child.getMatrix().mapPoints(position);
                offset.x = Math.round(position[0]);
                offset.y = Math.round(position[1]);
            }
            offset.x += dx;
            offset.y += dy;
        }
        final int width = mRight - mLeft;
        final int height = mBottom - mTop;
        boolean rectIsVisible = true;
        if (mParent == null || (mParent instanceof ViewGroup && ((ViewGroup) mParent).getClipChildren())) {
            rectIsVisible = rect.intersect(0, 0, width, height);
        }
        if (rectIsVisible && mClipToPadding) {
            rectIsVisible = rect.intersect(mPaddingLeft, mPaddingTop, width - mPaddingRight, height - mPaddingBottom);
        }
        r.set((int) Math.floor(rect.left), (int) Math.floor(rect.top), (int) Math.ceil(rect.right),
                (int) Math.ceil(rect.bottom));
        if (rectIsVisible && mParent != null) rectIsVisible = mParent.getChildVisibleRect(this, r, offset);
        return rectIsVisible;
    }

    @Override
    public boolean requestChildRectangleOnScreen(View child, Rect rectangle, boolean immediate) {
        return false;
    }

    @Override
    public boolean showContextMenuForChild(View originalView) {
        return mParent != null && mParent.showContextMenuForChild(originalView);
    }

    @Override
    public boolean showContextMenuForChild(View originalView, float x, float y) {
        return mParent != null && mParent.showContextMenuForChild(originalView, x, y);
    }

    @Override
    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback) {
        return startActionModeForChild(originalView, callback, ActionMode.TYPE_PRIMARY);
    }

    @Override
    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback, int type) {
        return mParent != null ? mParent.startActionModeForChild(originalView, callback, type) : null;
    }

    @Override
    public void childHasTransientStateChanged(View child, boolean childHasTransientState) {}

    @Override
    public void requestFitSystemWindows() {
        requestApplyInsets();
    }

    @Override
    public ViewParent getParentForAccessibility() {
        return mParent;
    }

    @Override
    public boolean isLayoutDirectionResolved() {
        return true;
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
    public boolean canResolveTextAlignment() {
        return true;
    }

    @Override
    public boolean isTextAlignmentResolved() {
        return true;
    }

    @Override
    public void focusableViewAvailable(View v) {
        if (mParent != null && getDescendantFocusability() != FOCUS_BLOCK_DESCENDANTS
                && (isFocusableInTouchMode() || !shouldBlockFocusForTouchscreen())
                && !(isFocused() && getDescendantFocusability() != FOCUS_AFTER_DESCENDANTS)) {
            mParent.focusableViewAvailable(v);
        }
    }

    @Override
    public View keyboardNavigationClusterSearch(View currentCluster, int direction) {
        return mParent != null ? mParent.keyboardNavigationClusterSearch(currentCluster, direction) : null;
    }

    // nested scrolling: no nested-scrolling parents among the framework's own containers.

    @Override
    public boolean onStartNestedScroll(View child, View target, int nestedScrollAxes) {
        return false;
    }

    @Override
    public void onNestedScrollAccepted(View child, View target, int axes) {}

    @Override
    public void onStopNestedScroll(View child) {}

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

    public int getNestedScrollAxes() {
        return SCROLL_AXIS_NONE;
    }

    public void offsetDescendantRectToMyCoords(View descendant, Rect rect) {
        offsetRectBetweenParentAndChild(descendant, rect, true, false);
    }

    public void offsetRectIntoDescendantCoords(View descendant, Rect rect) {
        offsetRectBetweenParentAndChild(descendant, rect, false, false);
    }

    void offsetRectBetweenParentAndChild(View descendant, Rect rect, boolean offsetFromChildToParent,
            boolean clipToBounds) {
        if (descendant == this) return;
        ViewParent theParent = descendant.mParent;
        while (theParent instanceof View && theParent != this) {
            if (offsetFromChildToParent) {
                rect.offset(descendant.mLeft - descendant.mScrollX, descendant.mTop - descendant.mScrollY);
                if (clipToBounds) {
                    View p = (View) theParent;
                    boolean intersected = rect.intersect(0, 0, p.mRight - p.mLeft, p.mBottom - p.mTop);
                    if (!intersected) rect.setEmpty();
                }
            } else {
                rect.offset(descendant.mScrollX - descendant.mLeft, descendant.mScrollY - descendant.mTop);
            }
            descendant = (View) theParent;
            theParent = descendant.mParent;
        }
        if (theParent == this) {
            if (offsetFromChildToParent) {
                rect.offset(descendant.mLeft - descendant.mScrollX, descendant.mTop - descendant.mScrollY);
            } else {
                rect.offset(descendant.mScrollX - descendant.mLeft, descendant.mScrollY - descendant.mTop);
            }
        } else {
            throw new IllegalArgumentException("parameter must be a descendant of this view");
        }
    }

    public void offsetChildrenTopAndBottom(int offset) {
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).offsetTopAndBottom(offset);
    }

    // --- focus -------------------------------------------------------------------------------------

    public int getDescendantFocusability() {
        return mDescendantFocusability;
    }

    public void setDescendantFocusability(int focusability) {
        switch (focusability) {
            case FOCUS_BEFORE_DESCENDANTS:
            case FOCUS_AFTER_DESCENDANTS:
            case FOCUS_BLOCK_DESCENDANTS:
                break;
            default:
                throw new IllegalArgumentException("must be one of FOCUS_BEFORE_DESCENDANTS, "
                        + "FOCUS_AFTER_DESCENDANTS, FOCUS_BLOCK_DESCENDANTS");
        }
        mDescendantFocusability = focusability;
        if (focusability == FOCUS_BLOCK_DESCENDANTS && mFocusedChild != null && !isFocused()) {
            mFocusedChild.clearFocus();
        }
    }

    public void setTouchscreenBlocksFocus(boolean touchscreenBlocksFocus) {
        mTouchscreenBlocksFocus = touchscreenBlocksFocus;
    }

    public boolean getTouchscreenBlocksFocus() {
        return mTouchscreenBlocksFocus;
    }

    boolean shouldBlockFocusForTouchscreen() {
        return mTouchscreenBlocksFocus && isInTouchMode();
    }

    @Override
    void unFocus(View focused) {
        if (mFocusedChild == null) {
            super.unFocus(focused);
        } else {
            mFocusedChild.unFocus(focused);
            mFocusedChild = null;
        }
    }

    public View getFocusedChild() {
        return mFocusedChild;
    }

    @Override
    public void requestChildFocus(View child, View focused) {
        if (getDescendantFocusability() == FOCUS_BLOCK_DESCENDANTS) return;
        // Unfocus this group itself; a descendant now has the focus.
        super.unFocus(focused);
        if (mFocusedChild != child) {
            if (mFocusedChild != null) mFocusedChild.unFocus(focused);
            mFocusedChild = child;
        }
        if (mParent != null) mParent.requestChildFocus(this, focused);
    }

    @Override
    public void clearChildFocus(View child) {
        mFocusedChild = null;
        if (mParent != null) mParent.clearChildFocus(this);
    }

    @Override
    public void clearFocus() {
        if (mFocusedChild == null) {
            super.clearFocus();
        } else {
            View focused = mFocusedChild;
            mFocusedChild = null;
            focused.clearFocus();
        }
    }

    @Override
    public boolean hasFocus() {
        return isFocused() || mFocusedChild != null;
    }

    @Override
    public View findFocus() {
        if (isFocused()) return this;
        if (mFocusedChild != null) return mFocusedChild.findFocus();
        return null;
    }

    @Override
    public boolean hasFocusable() {
        if (getVisibility() != VISIBLE) return false;
        if (isFocusable()) return true;
        if (getDescendantFocusability() != FOCUS_BLOCK_DESCENDANTS) {
            for (int i = 0; i < mChildren.size(); i++) {
                if (mChildren.get(i).hasFocusable()) return true;
            }
        }
        return false;
    }

    @Override
    public void addFocusables(ArrayList<View> views, int direction, int focusableMode) {
        final int focusableCount = views.size();
        final int descendantFocusability = getDescendantFocusability();
        final boolean focusSelf = isFocusableInTouchMode() || !shouldBlockFocusForTouchscreen();
        if (descendantFocusability == FOCUS_BLOCK_DESCENDANTS) {
            if (focusSelf) super.addFocusables(views, direction, focusableMode);
            return;
        }
        if (focusSelf && descendantFocusability == FOCUS_BEFORE_DESCENDANTS) {
            super.addFocusables(views, direction, focusableMode);
        }
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            if (child.getVisibility() == VISIBLE) child.addFocusables(views, direction, focusableMode);
        }
        if (focusSelf && descendantFocusability == FOCUS_AFTER_DESCENDANTS && focusableCount == views.size()) {
            super.addFocusables(views, direction, focusableMode);
        }
    }

    @Override
    public void addTouchables(ArrayList<View> views) {
        super.addTouchables(views);
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            if (child.getVisibility() == VISIBLE) child.addTouchables(views);
        }
    }

    @Override
    public View focusSearch(View focused, int direction) {
        if (mParent != null) return mParent.focusSearch(focused, direction);
        return FocusFinder.getInstance().findNextFocus(this, focused, direction);
    }

    @Override
    public boolean requestFocus(int direction, Rect previouslyFocusedRect) {
        final int descendantFocusability = getDescendantFocusability();
        boolean result;
        switch (descendantFocusability) {
            case FOCUS_BLOCK_DESCENDANTS:
                result = super.requestFocus(direction, previouslyFocusedRect);
                break;
            case FOCUS_BEFORE_DESCENDANTS: {
                final boolean took = super.requestFocus(direction, previouslyFocusedRect);
                result = took ? took : onRequestFocusInDescendants(direction, previouslyFocusedRect);
                break;
            }
            case FOCUS_AFTER_DESCENDANTS: {
                final boolean took = onRequestFocusInDescendants(direction, previouslyFocusedRect);
                result = took ? took : super.requestFocus(direction, previouslyFocusedRect);
                break;
            }
            default:
                throw new IllegalStateException("descendant focusability must be one of FOCUS_BEFORE_DESCENDANTS,"
                        + " FOCUS_AFTER_DESCENDANTS, FOCUS_BLOCK_DESCENDANTS but is " + descendantFocusability);
        }
        return result;
    }

    protected boolean onRequestFocusInDescendants(int direction, Rect previouslyFocusedRect) {
        int index;
        int increment;
        int end;
        int count = mChildren.size();
        if ((direction & FOCUS_FORWARD) != 0) {
            index = 0;
            increment = 1;
            end = count;
        } else {
            index = count - 1;
            increment = -1;
            end = -1;
        }
        for (int i = index; i != end; i += increment) {
            View child = mChildren.get(i);
            if (child.getVisibility() == VISIBLE && child.requestFocus(direction, previouslyFocusedRect)) {
                return true;
            }
        }
        return false;
    }

    /** Focuses the descendant marked focusedByDefault, or the first focusable one. */
    boolean restoreDefaultFocus() {
        View byDefault = findDefaultFocus(this);
        if (byDefault != null && byDefault.requestFocus()) return true;
        return requestFocus(FOCUS_DOWN);
    }

    private static View findDefaultFocus(View v) {
        if (v.isFocusedByDefault() && v.canTakeFocus()) return v;
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.mChildren.size(); i++) {
                View r = findDefaultFocus(g.mChildren.get(i));
                if (r != null) return r;
            }
        }
        return null;
    }

    // --- key and generic motion dispatch -----------------------------------------------------------

    @Override
    public boolean dispatchKeyEventPreIme(KeyEvent event) {
        if (isFocused()) return super.dispatchKeyEventPreIme(event);
        if (mFocusedChild != null) return mFocusedChild.dispatchKeyEventPreIme(event);
        return false;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (isFocused()) {
            if (super.dispatchKeyEvent(event)) return true;
        } else if (mFocusedChild != null) {
            if (mFocusedChild.dispatchKeyEvent(event)) return true;
        }
        return false;
    }

    @Override
    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        if (isFocused()) return super.dispatchKeyShortcutEvent(event);
        if (mFocusedChild != null) return mFocusedChild.dispatchKeyShortcutEvent(event);
        return false;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_CLASS_POINTER)) {
            final float x = event.getX();
            final float y = event.getY();
            final ArrayList<View> ordered = buildTouchDispatchChildList();
            for (int i = ordered.size() - 1; i >= 0; i--) {
                final View child = ordered.get(i);
                if (!canViewReceivePointerEvents(child) || !isTransformedTouchPointInView(x, y, child, null)) {
                    continue;
                }
                if (dispatchTransformedGenericPointerEvent(event, child)) return true;
            }
        } else if (mFocusedChild != null && !isFocused()) {
            if (mFocusedChild.dispatchGenericMotionEvent(event)) return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private boolean dispatchTransformedGenericPointerEvent(MotionEvent event, View child) {
        final float offsetX = mScrollX - child.mLeft;
        final float offsetY = mScrollY - child.mTop;
        if (child.hasIdentityMatrix()) {
            event.offsetLocation(offsetX, offsetY);
            boolean handled = child.dispatchGenericMotionEvent(event);
            event.offsetLocation(-offsetX, -offsetY);
            return handled;
        }
        MotionEvent transformed = MotionEvent.obtain(event);
        transformed.offsetLocation(offsetX, offsetY);
        transformed.transform(child.getInverseMatrix());
        boolean handled = child.dispatchGenericMotionEvent(transformed);
        transformed.recycle();
        return handled;
    }

    // --- touch dispatch -------------------------------------------------------------------------------

    public boolean onInterceptTouchEvent(MotionEvent ev) {
        return false;
    }

    public boolean onInterceptHoverEvent(MotionEvent event) {
        return false;
    }

    public void setMotionEventSplittingEnabled(boolean split) {
        mSplitMotionEvents = split;
    }

    public boolean isMotionEventSplittingEnabled() {
        return mSplitMotionEvents;
    }

    private static boolean canViewReceivePointerEvents(View child) {
        return child.getVisibility() == VISIBLE || child.getAnimation() != null;
    }

    /** True if (x, y) in this group's coordinates hits `child`; outLocalPoint gets child coordinates. */
    boolean isTransformedTouchPointInView(float x, float y, View child, android.graphics.PointF outLocalPoint) {
        float[] point = {x + mScrollX - child.mLeft, y + mScrollY - child.mTop};
        if (!child.hasIdentityMatrix()) child.getInverseMatrix().mapPoints(point);
        final boolean isInView = child.pointInView(point[0], point[1], 0);
        if (isInView && outLocalPoint != null) outLocalPoint.set(point[0], point[1]);
        return isInView;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        boolean handled = false;
        if (!onFilterTouchEventForSecurity(ev)) return false;
        final int actionMasked = ev.getActionMasked();
        if (actionMasked == MotionEvent.ACTION_DOWN) {
            // A new gesture: drop the previous target and any intercept request.
            if (mFirstTouchTarget != null) {
                dispatchTransformedTouchEvent(ev, true, mFirstTouchTarget);
                mFirstTouchTarget = null;
            }
            mDisallowIntercept = false;
        }

        final boolean intercepted;
        if (actionMasked == MotionEvent.ACTION_DOWN || mFirstTouchTarget != null) {
            if (!mDisallowIntercept) {
                final int action = ev.getAction();
                intercepted = onInterceptTouchEvent(ev);
                ev.setAction(action);
            } else {
                intercepted = false;
            }
        } else {
            intercepted = true;
        }

        final boolean canceled = actionMasked == MotionEvent.ACTION_CANCEL;
        boolean alreadyDispatchedToNewTouchTarget = false;
        if (!canceled && !intercepted && actionMasked == MotionEvent.ACTION_DOWN) {
            final float x = ev.getX();
            final float y = ev.getY();
            final ArrayList<View> ordered = buildTouchDispatchChildList();
            for (int i = ordered.size() - 1; i >= 0; i--) {
                final View child = ordered.get(i);
                if (!canViewReceivePointerEvents(child) || !isTransformedTouchPointInView(x, y, child, null)) {
                    continue;
                }
                if (dispatchTransformedTouchEvent(ev, false, child)) {
                    mFirstTouchTarget = child;
                    alreadyDispatchedToNewTouchTarget = true;
                    break;
                }
            }
        }

        if (mFirstTouchTarget == null) {
            handled = dispatchTransformedTouchEvent(ev, canceled, null);
        } else if (alreadyDispatchedToNewTouchTarget) {
            handled = true;
        } else {
            final View target = mFirstTouchTarget;
            final boolean cancelChild = intercepted;
            if (dispatchTransformedTouchEvent(ev, cancelChild, target)) handled = true;
            if (cancelChild) mFirstTouchTarget = null;
        }

        if (canceled || actionMasked == MotionEvent.ACTION_UP) {
            mFirstTouchTarget = null;
            mDisallowIntercept = false;
        }
        return handled;
    }

    private boolean dispatchTransformedTouchEvent(MotionEvent event, boolean cancel, View child) {
        final int oldAction = event.getAction();
        if (cancel) event.setAction(MotionEvent.ACTION_CANCEL);
        boolean handled;
        if (child == null) {
            handled = super.dispatchTouchEvent(event);
        } else {
            final float offsetX = mScrollX - child.mLeft;
            final float offsetY = mScrollY - child.mTop;
            if (child.hasIdentityMatrix()) {
                event.offsetLocation(offsetX, offsetY);
                handled = child.dispatchTouchEvent(event);
                event.offsetLocation(-offsetX, -offsetY);
            } else {
                MotionEvent transformed = MotionEvent.obtain(event);
                transformed.offsetLocation(offsetX, offsetY);
                transformed.transform(child.getInverseMatrix());
                handled = child.dispatchTouchEvent(transformed);
                transformed.recycle();
            }
        }
        event.setAction(oldAction);
        return handled;
    }

    /** Children in dispatch order: drawing order, stably sorted by Z like AOSP's buildOrderedChildList. */
    private ArrayList<View> buildTouchDispatchChildList() {
        final int count = mChildren.size();
        ArrayList<View> list = new ArrayList<View>(count);
        boolean hasZ = false;
        for (int i = 0; i < count; i++) {
            View child = mChildren.get(getAndVerifyPreorderedIndex(count, i));
            if (child.getZ() != 0) hasZ = true;
            list.add(child);
        }
        if (hasZ) {
            // insertion sort keeps equal-Z children in drawing order
            for (int i = 1; i < count; i++) {
                View v = list.get(i);
                float z = v.getZ();
                int j = i - 1;
                while (j >= 0 && list.get(j).getZ() > z) {
                    list.set(j + 1, list.get(j));
                    j--;
                }
                list.set(j + 1, v);
            }
        }
        return list;
    }

    private int getAndVerifyPreorderedIndex(int childrenCount, int i) {
        if (!mChildrenDrawingOrderEnabled) return i;
        final int childIndex = getChildDrawingOrder(childrenCount, i);
        if (childIndex >= childrenCount) {
            throw new IndexOutOfBoundsException("getChildDrawingOrder() returned invalid index " + childIndex
                    + " (child count is " + childrenCount + ")");
        }
        return childIndex;
    }

    protected int getChildDrawingOrder(int childCount, int drawingPosition) {
        return drawingPosition;
    }

    public final int getChildDrawingOrder(int drawingPosition) {
        return getChildDrawingOrder(getChildCount(), drawingPosition);
    }

    protected boolean isChildrenDrawingOrderEnabled() {
        return mChildrenDrawingOrderEnabled;
    }

    protected void setChildrenDrawingOrderEnabled(boolean enabled) {
        mChildrenDrawingOrderEnabled = enabled;
    }

    // --- pressed / selected / activated and drawable state ------------------------------------------

    @Override
    protected void dispatchSetPressed(boolean pressed) {
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            // Children that are clickable on their own show their own pressed state.
            if (!pressed || (!child.isClickable() && !child.isLongClickable())) child.setPressed(pressed);
        }
    }

    @Override
    public void dispatchSetSelected(boolean selected) {
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).setSelected(selected);
    }

    @Override
    public void dispatchSetActivated(boolean activated) {
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).setActivated(activated);
    }

    @Override
    public void dispatchDrawableHotspotChanged(float x, float y) {
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            final boolean nonActionable = !child.isClickable() && !child.isLongClickable();
            if (nonActionable || child.isDuplicateParentStateEnabled()) {
                float[] point = {x - child.mLeft + mScrollX, y - child.mTop + mScrollY};
                child.drawableHotspotChanged(point[0], point[1]);
            }
        }
    }

    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        for (int i = 0; i < mChildren.size(); i++) {
            final View child = mChildren.get(i);
            if (child.isDuplicateParentStateEnabled()) child.refreshDrawableState();
        }
    }

    @Override
    protected int[] onCreateDrawableState(int extraSpace) {
        if (!mAddStatesFromChildren) return super.onCreateDrawableState(extraSpace);
        int need = 0;
        for (int i = 0; i < mChildren.size(); i++) {
            need += mChildren.get(i).getDrawableState().length;
        }
        int[] state = super.onCreateDrawableState(extraSpace + need);
        for (int i = 0; i < mChildren.size(); i++) {
            int[] childState = mChildren.get(i).getDrawableState();
            if (childState.length > 0) state = mergeDrawableStates(state, childState);
        }
        return state;
    }

    public void setAddStatesFromChildren(boolean addsStates) {
        mAddStatesFromChildren = addsStates;
        refreshDrawableState();
    }

    public boolean addStatesFromChildren() {
        return mAddStatesFromChildren;
    }

    @Override
    public void childDrawableStateChanged(View child) {
        if (mAddStatesFromChildren) refreshDrawableState();
    }

    @Override
    public void jumpDrawablesToCurrentState() {
        super.jumpDrawablesToCurrentState();
        for (int i = 0; i < mChildren.size(); i++) mChildren.get(i).jumpDrawablesToCurrentState();
    }

    // --- saved state ------------------------------------------------------------------------------------

    @Override
    protected void dispatchSaveInstanceState(SparseArray<Parcelable> container) {
        super.dispatchSaveInstanceState(container);
        for (int i = 0; i < mChildren.size(); i++) {
            View c = mChildren.get(i);
            if (c.isSaveFromParentEnabled()) c.dispatchSaveInstanceState(container);
        }
    }

    protected void dispatchFreezeSelfOnly(SparseArray<Parcelable> container) {
        super.dispatchSaveInstanceState(container);
    }

    @Override
    protected void dispatchRestoreInstanceState(SparseArray<Parcelable> container) {
        super.dispatchRestoreInstanceState(container);
        for (int i = 0; i < mChildren.size(); i++) {
            View c = mChildren.get(i);
            if (c.isSaveFromParentEnabled()) c.dispatchRestoreInstanceState(container);
        }
    }

    protected void dispatchThawSelfOnly(SparseArray<Parcelable> container) {
        super.dispatchRestoreInstanceState(container);
    }

    // --- drawing ---------------------------------------------------------------------------------------

    public void setClipChildren(boolean clipChildren) {
        mClipChildren = clipChildren;
        invalidate();
    }

    public boolean getClipChildren() {
        return mClipChildren;
    }

    public void setClipToPadding(boolean clipToPadding) {
        if (mClipToPadding != clipToPadding) {
            mClipToPadding = clipToPadding;
            invalidate();
        }
    }

    public boolean getClipToPadding() {
        return mClipToPadding;
    }

    public int getPersistentDrawingCache() {
        return mPersistentDrawingCache;
    }

    public void setPersistentDrawingCache(int drawingCacheToKeep) {
        mPersistentDrawingCache = drawingCacheToKeep & PERSISTENT_ALL_CACHES;
    }

    public void setAlwaysDrawnWithCacheEnabled(boolean always) {}

    public boolean isAlwaysDrawnWithCacheEnabled() {
        return false;
    }

    protected void setStaticTransformationsEnabled(boolean enabled) {
        mStaticTransformationsEnabled = enabled;
    }

    protected boolean getChildStaticTransformation(View child, Transformation t) {
        return false;
    }

    @Override
    protected void dispatchDraw(Canvas canvas) {
        final int count = mChildren.size();
        int clipSaveCount = 0;
        if (mClipToPadding) {
            clipSaveCount = canvas.save();
            canvas.clipRect(mScrollX + mPaddingLeft, mScrollY + mPaddingTop,
                    mScrollX + mRight - mLeft - mPaddingRight, mScrollY + mBottom - mTop - mPaddingBottom);
        }
        final long drawingTime = getDrawingTime();
        final ArrayList<View> ordered = buildTouchDispatchChildList();
        for (int i = 0; i < count; i++) {
            final View child = ordered.get(i);
            if (child.getVisibility() == VISIBLE || child.getAnimation() != null) drawChild(canvas, child, drawingTime);
        }
        if (mClipToPadding) canvas.restoreToCount(clipSaveCount);
    }

    /**
     * Draws one child: its view animation, transform, alpha and clip, then the
     * child's own content (AOSP View.draw(Canvas, ViewGroup, long), software path).
     */
    protected boolean drawChild(Canvas canvas, View child, long drawingTime) {
        final Transformation anim = child.applyAnimation(drawingTime, this);
        final boolean more = anim != null && child.getAnimation() != null;
        Transformation staticT = null;
        if (mStaticTransformationsEnabled) {
            mChildTransformation.clear();
            if (getChildStaticTransformation(child, mChildTransformation)) staticT = mChildTransformation;
        }
        float alpha = child.getFinalAlpha();
        if (anim != null) alpha *= anim.getAlpha();
        if (staticT != null) alpha *= staticT.getAlpha();
        if (alpha <= 0f) return more;

        final int width = child.mRight - child.mLeft;
        final int height = child.mBottom - child.mTop;
        final int save = canvas.save();
        canvas.translate(child.mLeft, child.mTop);
        if (anim != null && (anim.getTransformationType() & Transformation.TYPE_MATRIX) != 0) {
            canvas.concat(anim.getMatrix());
        }
        if (staticT != null && (staticT.getTransformationType() & Transformation.TYPE_MATRIX) != 0) {
            canvas.concat(staticT.getMatrix());
        }
        if (!child.hasIdentityMatrix()) canvas.concat(child.getMatrix());
        if (mClipChildren) canvas.clipRect(0, 0, width, height);
        Rect clipBounds = new Rect();
        if (child.getClipBounds(clipBounds)) canvas.clipRect(clipBounds);
        canvas.translate(-child.mScrollX, -child.mScrollY);
        if (alpha < 1f) {
            canvas.saveLayerAlpha(child.mScrollX, child.mScrollY, child.mScrollX + width, child.mScrollY + height,
                    (int) (alpha * 255));
        }
        child.computeScroll();
        if (child.skipsDraw()) {
            child.dispatchDraw(canvas);
        } else {
            child.draw(canvas);
        }
        canvas.restoreToCount(save);
        return more;
    }

    public boolean shouldDelayChildPressedState() {
        return true;
    }

    @Override
    public CharSequence getAccessibilityClassName() {
        return ViewGroup.class.getName();
    }

}
