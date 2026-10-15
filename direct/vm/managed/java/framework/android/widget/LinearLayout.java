package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;

/**
 * Stacks children in a row or column, distributing leftover space by weight
 * (the measure pass follows LinearLayout.measureVertical/measureHorizontal;
 * baseline alignment and dividers are not implemented yet).
 */
public class LinearLayout extends ViewGroup {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public static class LayoutParams extends MarginLayoutParams {
        public float weight;
        public int gravity = -1;

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, float weight) {
            super(width, height);
            this.weight = weight;
        }

        public LayoutParams(ViewGroup.LayoutParams p) {
            super(p);
        }

        public LayoutParams(MarginLayoutParams source) {
            super(source);
        }

        public LayoutParams(LayoutParams source) {
            super(source);
            weight = source.weight;
            gravity = source.gravity;
        }
    }

    private int mOrientation = HORIZONTAL;
    private int mGravity = Gravity.START | Gravity.TOP;
    private float mWeightSum = -1f;
    private int mTotalLength;

    public LinearLayout(Context context) {
        super(context);
    }

    public LinearLayout(Context context, AttributeSet attrs) {
        this(context);
    }

    public LinearLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setOrientation(int orientation) {
        if (mOrientation != orientation) {
            mOrientation = orientation;
            requestLayout();
        }
    }

    public int getOrientation() {
        return mOrientation;
    }

    public void setGravity(int gravity) {
        if (mGravity != gravity) {
            if ((gravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) == 0) gravity |= Gravity.START;
            if ((gravity & Gravity.VERTICAL_GRAVITY_MASK) == 0) gravity |= Gravity.TOP;
            mGravity = gravity;
            requestLayout();
        }
    }

    public int getGravity() {
        return mGravity;
    }

    public void setHorizontalGravity(int horizontalGravity) {
        setGravity((mGravity & ~Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK)
                | (horizontalGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK));
    }

    public void setVerticalGravity(int verticalGravity) {
        setGravity((mGravity & ~Gravity.VERTICAL_GRAVITY_MASK) | (verticalGravity & Gravity.VERTICAL_GRAVITY_MASK));
    }

    public float getWeightSum() {
        return mWeightSum;
    }

    public void setWeightSum(float weightSum) {
        mWeightSum = Math.max(0.0f, weightSum);
    }

    public boolean isBaselineAligned() {
        return false;
    }

    public void setBaselineAligned(boolean baselineAligned) {}

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        if (mOrientation == HORIZONTAL) {
            return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        }
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams;
    }

    @Override
    protected ViewGroup.LayoutParams generateLayoutParams(ViewGroup.LayoutParams lp) {
        if (lp instanceof LayoutParams) return new LayoutParams((LayoutParams) lp);
        if (lp instanceof MarginLayoutParams) return new LayoutParams((MarginLayoutParams) lp);
        return new LayoutParams(lp);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        if (mOrientation == VERTICAL) {
            measureVertical(widthMeasureSpec, heightMeasureSpec);
        } else {
            measureHorizontal(widthMeasureSpec, heightMeasureSpec);
        }
    }

    void measureVertical(int widthMeasureSpec, int heightMeasureSpec) {
        mTotalLength = 0;
        int maxWidth = 0;
        int childState = 0;
        float totalWeight = 0;
        boolean matchWidth = false;
        final int count = getChildCount();
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        boolean skippedMeasure = false;

        for (int i = 0; i < count; ++i) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            totalWeight += lp.weight;
            final boolean useExcessSpace = lp.height == 0 && lp.weight > 0;
            if (heightMode == MeasureSpec.EXACTLY && useExcessSpace) {
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength, totalLength + lp.topMargin + lp.bottomMargin);
                skippedMeasure = true;
            } else {
                if (useExcessSpace) lp.height = LayoutParams.WRAP_CONTENT;
                final int usedHeight = totalWeight == 0 ? mTotalLength : 0;
                measureChildWithMargins(child, widthMeasureSpec, 0, heightMeasureSpec, usedHeight);
                final int childHeight = child.getMeasuredHeight();
                if (useExcessSpace) lp.height = 0;
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength, totalLength + childHeight + lp.topMargin + lp.bottomMargin);
            }
            boolean matchWidthLocally = false;
            if (widthMode != MeasureSpec.EXACTLY && lp.width == LayoutParams.MATCH_PARENT) {
                matchWidth = true;
                matchWidthLocally = true;
            }
            final int margin = lp.leftMargin + lp.rightMargin;
            final int measuredWidth = child.getMeasuredWidth() + margin;
            maxWidth = Math.max(maxWidth, matchWidthLocally ? margin : measuredWidth);
            childState = combineMeasuredStates(childState, child.getMeasuredState());
        }

        mTotalLength += getPaddingTop() + getPaddingBottom();
        int heightSize = Math.max(mTotalLength, getSuggestedMinimumHeight());
        int heightSizeAndState = resolveSizeAndState(heightSize, heightMeasureSpec, 0);
        heightSize = heightSizeAndState & MEASURED_SIZE_MASK;
        int remainingExcess = heightSize - mTotalLength;

        if (skippedMeasure || (totalWeight > 0.0f && remainingExcess != 0)) {
            float remainingWeightSum = mWeightSum > 0.0f ? mWeightSum : totalWeight;
            mTotalLength = 0;
            maxWidth = 0;
            for (int i = 0; i < count; ++i) {
                final View child = getChildAt(i);
                if (child.getVisibility() == GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                final float childWeight = lp.weight;
                if (childWeight > 0) {
                    final int share = (int) (childWeight * remainingExcess / remainingWeightSum);
                    remainingExcess -= share;
                    remainingWeightSum -= childWeight;
                    final int childHeight;
                    if (lp.height == 0 && heightMode == MeasureSpec.EXACTLY) {
                        childHeight = share;
                    } else {
                        childHeight = child.getMeasuredHeight() + share;
                    }
                    final int childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(Math.max(0, childHeight),
                            MeasureSpec.EXACTLY);
                    final int childWidthMeasureSpec = getChildMeasureSpec(widthMeasureSpec,
                            getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin, lp.width);
                    child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                    childState = combineMeasuredStates(childState, child.getMeasuredState() & (MEASURED_STATE_MASK
                            >> MEASURED_HEIGHT_STATE_SHIFT));
                }
                final int margin = lp.leftMargin + lp.rightMargin;
                final int measuredWidth = child.getMeasuredWidth() + margin;
                boolean matchWidthLocally = widthMode != MeasureSpec.EXACTLY && lp.width == LayoutParams.MATCH_PARENT;
                maxWidth = Math.max(maxWidth, matchWidthLocally ? margin : measuredWidth);
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength, totalLength + child.getMeasuredHeight() + lp.topMargin
                        + lp.bottomMargin);
            }
            mTotalLength += getPaddingTop() + getPaddingBottom();
        }

        maxWidth += getPaddingLeft() + getPaddingRight();
        maxWidth = Math.max(maxWidth, getSuggestedMinimumWidth());
        setMeasuredDimension(resolveSizeAndState(maxWidth, widthMeasureSpec, childState), heightSizeAndState);
        if (matchWidth) forceUniformWidth(count, heightMeasureSpec);
    }

    private void forceUniformWidth(int count, int heightMeasureSpec) {
        int uniformMeasureSpec = MeasureSpec.makeMeasureSpec(getMeasuredWidth(), MeasureSpec.EXACTLY);
        for (int i = 0; i < count; ++i) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            LayoutParams lp = (LayoutParams) child.getLayoutParams();
            if (lp.width == LayoutParams.MATCH_PARENT) {
                int oldHeight = lp.height;
                lp.height = child.getMeasuredHeight();
                measureChildWithMargins(child, uniformMeasureSpec, 0, heightMeasureSpec, 0);
                lp.height = oldHeight;
            }
        }
    }

    void measureHorizontal(int widthMeasureSpec, int heightMeasureSpec) {
        mTotalLength = 0;
        int maxHeight = 0;
        int childState = 0;
        float totalWeight = 0;
        boolean matchHeight = false;
        final int count = getChildCount();
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        boolean skippedMeasure = false;

        for (int i = 0; i < count; ++i) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            totalWeight += lp.weight;
            final boolean useExcessSpace = lp.width == 0 && lp.weight > 0;
            if (widthMode == MeasureSpec.EXACTLY && useExcessSpace) {
                mTotalLength += lp.leftMargin + lp.rightMargin;
                skippedMeasure = true;
            } else {
                if (useExcessSpace) lp.width = LayoutParams.WRAP_CONTENT;
                final int usedWidth = totalWeight == 0 ? mTotalLength : 0;
                measureChildWithMargins(child, widthMeasureSpec, usedWidth, heightMeasureSpec, 0);
                final int childWidth = child.getMeasuredWidth();
                if (useExcessSpace) lp.width = 0;
                mTotalLength = Math.max(mTotalLength, mTotalLength + childWidth + lp.leftMargin + lp.rightMargin);
            }
            boolean matchHeightLocally = false;
            if (heightMode != MeasureSpec.EXACTLY && lp.height == LayoutParams.MATCH_PARENT) {
                matchHeight = true;
                matchHeightLocally = true;
            }
            final int margin = lp.topMargin + lp.bottomMargin;
            final int childHeight = child.getMeasuredHeight() + margin;
            maxHeight = Math.max(maxHeight, matchHeightLocally ? margin : childHeight);
            childState = combineMeasuredStates(childState, child.getMeasuredState());
        }

        mTotalLength += getPaddingLeft() + getPaddingRight();
        int widthSize = Math.max(mTotalLength, getSuggestedMinimumWidth());
        int widthSizeAndState = resolveSizeAndState(widthSize, widthMeasureSpec, 0);
        widthSize = widthSizeAndState & MEASURED_SIZE_MASK;
        int remainingExcess = widthSize - mTotalLength;

        if (skippedMeasure || (totalWeight > 0.0f && remainingExcess != 0)) {
            float remainingWeightSum = mWeightSum > 0.0f ? mWeightSum : totalWeight;
            mTotalLength = 0;
            maxHeight = 0;
            for (int i = 0; i < count; ++i) {
                final View child = getChildAt(i);
                if (child.getVisibility() == GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                final float childWeight = lp.weight;
                if (childWeight > 0) {
                    final int share = (int) (childWeight * remainingExcess / remainingWeightSum);
                    remainingExcess -= share;
                    remainingWeightSum -= childWeight;
                    final int childWidth;
                    if (lp.width == 0 && widthMode == MeasureSpec.EXACTLY) {
                        childWidth = share;
                    } else {
                        childWidth = child.getMeasuredWidth() + share;
                    }
                    final int childWidthMeasureSpec = MeasureSpec.makeMeasureSpec(Math.max(0, childWidth),
                            MeasureSpec.EXACTLY);
                    final int childHeightMeasureSpec = getChildMeasureSpec(heightMeasureSpec,
                            getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin, lp.height);
                    child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                    childState = combineMeasuredStates(childState, child.getMeasuredState() & MEASURED_STATE_MASK);
                }
                mTotalLength = Math.max(mTotalLength, mTotalLength + child.getMeasuredWidth() + lp.leftMargin
                        + lp.rightMargin);
                final int margin = lp.topMargin + lp.bottomMargin;
                boolean matchHeightLocally = heightMode != MeasureSpec.EXACTLY
                        && lp.height == LayoutParams.MATCH_PARENT;
                maxHeight = Math.max(maxHeight, matchHeightLocally ? margin : child.getMeasuredHeight() + margin);
            }
            mTotalLength += getPaddingLeft() + getPaddingRight();
        }

        maxHeight += getPaddingTop() + getPaddingBottom();
        maxHeight = Math.max(maxHeight, getSuggestedMinimumHeight());
        setMeasuredDimension(widthSizeAndState | (childState & MEASURED_STATE_MASK),
                resolveSizeAndState(maxHeight, heightMeasureSpec, (childState << MEASURED_HEIGHT_STATE_SHIFT)));
        if (matchHeight) forceUniformHeight(count, widthMeasureSpec);
    }

    private void forceUniformHeight(int count, int widthMeasureSpec) {
        int uniformMeasureSpec = MeasureSpec.makeMeasureSpec(getMeasuredHeight(), MeasureSpec.EXACTLY);
        for (int i = 0; i < count; ++i) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            LayoutParams lp = (LayoutParams) child.getLayoutParams();
            if (lp.height == LayoutParams.MATCH_PARENT) {
                int oldWidth = lp.width;
                lp.width = child.getMeasuredWidth();
                measureChildWithMargins(child, widthMeasureSpec, 0, uniformMeasureSpec, 0);
                lp.width = oldWidth;
            }
        }
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (mOrientation == VERTICAL) {
            layoutVertical(l, t, r, b);
        } else {
            layoutHorizontal(l, t, r, b);
        }
    }

    void layoutVertical(int left, int top, int right, int bottom) {
        final int paddingLeft = getPaddingLeft();
        final int width = right - left;
        final int childRight = width - getPaddingRight();
        final int childSpace = width - paddingLeft - getPaddingRight();
        final int count = getChildCount();
        final int majorGravity = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
        final int minorGravity = mGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;
        int childTop;
        switch (majorGravity) {
            case Gravity.BOTTOM:
                childTop = getPaddingTop() + bottom - top - mTotalLength;
                break;
            case Gravity.CENTER_VERTICAL:
                childTop = getPaddingTop() + (bottom - top - mTotalLength) / 2;
                break;
            case Gravity.TOP:
            default:
                childTop = getPaddingTop();
                break;
        }
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            final int childWidth = child.getMeasuredWidth();
            final int childHeight = child.getMeasuredHeight();
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            int gravity = lp.gravity;
            if (gravity < 0) gravity = minorGravity;
            final int absoluteGravity = Gravity.getAbsoluteGravity(gravity, getLayoutDirection());
            int childLeft;
            switch (absoluteGravity & Gravity.HORIZONTAL_GRAVITY_MASK) {
                case Gravity.CENTER_HORIZONTAL:
                    childLeft = paddingLeft + ((childSpace - childWidth) / 2) + lp.leftMargin - lp.rightMargin;
                    break;
                case Gravity.RIGHT:
                    childLeft = childRight - childWidth - lp.rightMargin;
                    break;
                case Gravity.LEFT:
                default:
                    childLeft = paddingLeft + lp.leftMargin;
                    break;
            }
            childTop += lp.topMargin;
            child.layout(childLeft, childTop, childLeft + childWidth, childTop + childHeight);
            childTop += childHeight + lp.bottomMargin;
        }
    }

    void layoutHorizontal(int left, int top, int right, int bottom) {
        final int paddingTop = getPaddingTop();
        final int height = bottom - top;
        final int childBottom = height - getPaddingBottom();
        final int childSpace = height - paddingTop - getPaddingBottom();
        final int count = getChildCount();
        final int majorGravity = Gravity.getAbsoluteGravity(mGravity, getLayoutDirection())
                & Gravity.HORIZONTAL_GRAVITY_MASK;
        final int minorGravity = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
        int childLeft;
        switch (majorGravity) {
            case Gravity.RIGHT:
                childLeft = getPaddingLeft() + right - left - mTotalLength;
                break;
            case Gravity.CENTER_HORIZONTAL:
                childLeft = getPaddingLeft() + (right - left - mTotalLength) / 2;
                break;
            case Gravity.LEFT:
            default:
                childLeft = getPaddingLeft();
                break;
        }
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            final int childWidth = child.getMeasuredWidth();
            final int childHeight = child.getMeasuredHeight();
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            int gravity = lp.gravity;
            if (gravity < 0) gravity = minorGravity;
            int childTop;
            switch (gravity & Gravity.VERTICAL_GRAVITY_MASK) {
                case Gravity.TOP:
                    childTop = paddingTop + lp.topMargin;
                    break;
                case Gravity.CENTER_VERTICAL:
                    childTop = paddingTop + ((childSpace - childHeight) / 2) + lp.topMargin - lp.bottomMargin;
                    break;
                case Gravity.BOTTOM:
                    childTop = childBottom - childHeight - lp.bottomMargin;
                    break;
                default:
                    childTop = paddingTop;
                    break;
            }
            childLeft += lp.leftMargin;
            child.layout(childLeft, childTop, childLeft + childWidth, childTop + childHeight);
            childLeft += childWidth + lp.rightMargin;
        }
    }
}
