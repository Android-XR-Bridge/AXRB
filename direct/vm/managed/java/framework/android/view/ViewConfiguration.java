package android.view;

import android.content.Context;
import android.util.DisplayMetrics;

/** Standard UI timeouts and distances (AOSP defaults, scaled by density). */
public class ViewConfiguration {
    private static final int SCROLL_BAR_SIZE = 4;
    private static final int SCROLL_BAR_FADE_DURATION = 250;
    private static final int SCROLL_BAR_DEFAULT_DELAY = 300;
    private static final int FADING_EDGE_LENGTH = 12;
    private static final int PRESSED_STATE_DURATION = 64;
    private static final int DEFAULT_LONG_PRESS_TIMEOUT = 500;
    private static final int KEY_REPEAT_TIMEOUT = 400;
    private static final int KEY_REPEAT_DELAY = 50;
    private static final int TAP_TIMEOUT = 100;
    private static final int JUMP_TAP_TIMEOUT = 500;
    private static final int DOUBLE_TAP_TIMEOUT = 300;
    private static final int DOUBLE_TAP_MIN_TIME = 40;
    private static final int ZOOM_CONTROLS_TIMEOUT = 3000;
    private static final int EDGE_SLOP = 12;
    private static final int TOUCH_SLOP = 8;
    private static final int DOUBLE_TAP_TOUCH_SLOP = TOUCH_SLOP;
    private static final int PAGING_TOUCH_SLOP = TOUCH_SLOP * 2;
    private static final int DOUBLE_TAP_SLOP = 100;
    private static final int WINDOW_TOUCH_SLOP = 16;
    private static final int MINIMUM_FLING_VELOCITY = 50;
    private static final int MAXIMUM_FLING_VELOCITY = 8000;
    private static final int OVERSCROLL_DISTANCE = 0;
    private static final int OVERFLING_DISTANCE = 6;
    private static final float SCROLL_FRICTION = 0.015f;
    private static final int SCROLL_FACTOR_DIP = 64;

    private static ViewConfiguration sDefault;

    private final int mEdgeSlop;
    private final int mFadingEdgeLength;
    private final int mMinimumFlingVelocity;
    private final int mMaximumFlingVelocity;
    private final int mScrollbarSize;
    private final int mTouchSlop;
    private final int mDoubleTapTouchSlop;
    private final int mPagingTouchSlop;
    private final int mDoubleTapSlop;
    private final int mWindowTouchSlop;
    private final int mOverscrollDistance;
    private final int mOverflingDistance;
    private final float mScrollFactor;

    @Deprecated
    public ViewConfiguration() {
        this(1f);
    }

    private ViewConfiguration(float density) {
        mEdgeSlop = (int) (density * EDGE_SLOP + 0.5f);
        mFadingEdgeLength = (int) (density * FADING_EDGE_LENGTH + 0.5f);
        mMinimumFlingVelocity = (int) (density * MINIMUM_FLING_VELOCITY + 0.5f);
        mMaximumFlingVelocity = (int) (density * MAXIMUM_FLING_VELOCITY + 0.5f);
        mScrollbarSize = (int) (density * SCROLL_BAR_SIZE + 0.5f);
        mTouchSlop = (int) (density * TOUCH_SLOP + 0.5f);
        mDoubleTapTouchSlop = mTouchSlop;
        mPagingTouchSlop = mTouchSlop * 2;
        mDoubleTapSlop = (int) (density * DOUBLE_TAP_SLOP + 0.5f);
        mWindowTouchSlop = (int) (density * WINDOW_TOUCH_SLOP + 0.5f);
        mOverscrollDistance = (int) (density * OVERSCROLL_DISTANCE + 0.5f);
        mOverflingDistance = (int) (density * OVERFLING_DISTANCE + 0.5f);
        mScrollFactor = density * SCROLL_FACTOR_DIP;
    }

    public static ViewConfiguration get(Context context) {
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        synchronized (ViewConfiguration.class) {
            if (sDefault == null || sDefault.mTouchSlop != (int) (metrics.density * TOUCH_SLOP + 0.5f)) {
                sDefault = new ViewConfiguration(metrics.density);
            }
            return sDefault;
        }
    }

    public static int getScrollBarSize() {
        return SCROLL_BAR_SIZE;
    }

    public int getScaledScrollBarSize() {
        return mScrollbarSize;
    }

    public static int getScrollBarFadeDuration() {
        return SCROLL_BAR_FADE_DURATION;
    }

    public static int getScrollDefaultDelay() {
        return SCROLL_BAR_DEFAULT_DELAY;
    }

    public static int getFadingEdgeLength() {
        return FADING_EDGE_LENGTH;
    }

    public int getScaledFadingEdgeLength() {
        return mFadingEdgeLength;
    }

    public static int getPressedStateDuration() {
        return PRESSED_STATE_DURATION;
    }

    public static int getLongPressTimeout() {
        return DEFAULT_LONG_PRESS_TIMEOUT;
    }

    public static int getKeyRepeatTimeout() {
        return KEY_REPEAT_TIMEOUT;
    }

    public static int getKeyRepeatDelay() {
        return KEY_REPEAT_DELAY;
    }

    public static int getTapTimeout() {
        return TAP_TIMEOUT;
    }

    public static int getJumpTapTimeout() {
        return JUMP_TAP_TIMEOUT;
    }

    public static int getDoubleTapTimeout() {
        return DOUBLE_TAP_TIMEOUT;
    }

    public static int getDoubleTapMinTime() {
        return DOUBLE_TAP_MIN_TIME;
    }

    public static int getEdgeSlop() {
        return EDGE_SLOP;
    }

    public int getScaledEdgeSlop() {
        return mEdgeSlop;
    }

    public static int getTouchSlop() {
        return TOUCH_SLOP;
    }

    public int getScaledTouchSlop() {
        return mTouchSlop;
    }

    public int getScaledHoverSlop() {
        return mTouchSlop / 2;
    }

    public int getScaledDoubleTapTouchSlop() {
        return mDoubleTapTouchSlop;
    }

    public int getScaledPagingTouchSlop() {
        return mPagingTouchSlop;
    }

    public static int getDoubleTapSlop() {
        return DOUBLE_TAP_SLOP;
    }

    public int getScaledDoubleTapSlop() {
        return mDoubleTapSlop;
    }

    public static int getWindowTouchSlop() {
        return WINDOW_TOUCH_SLOP;
    }

    public int getScaledWindowTouchSlop() {
        return mWindowTouchSlop;
    }

    public static int getMinimumFlingVelocity() {
        return MINIMUM_FLING_VELOCITY;
    }

    public int getScaledMinimumFlingVelocity() {
        return mMinimumFlingVelocity;
    }

    public static int getMaximumFlingVelocity() {
        return MAXIMUM_FLING_VELOCITY;
    }

    public int getScaledMaximumFlingVelocity() {
        return mMaximumFlingVelocity;
    }

    public int getScaledOverscrollDistance() {
        return mOverscrollDistance;
    }

    public int getScaledOverflingDistance() {
        return mOverflingDistance;
    }

    public float getScaledVerticalScrollFactor() {
        return mScrollFactor;
    }

    public float getScaledHorizontalScrollFactor() {
        return mScrollFactor;
    }

    public static long getZoomControlsTimeout() {
        return ZOOM_CONTROLS_TIMEOUT;
    }

    public static long getGlobalActionKeyTimeout() {
        return DEFAULT_LONG_PRESS_TIMEOUT;
    }

    public static float getScrollFriction() {
        return SCROLL_FRICTION;
    }

    public static int getMultiPressTimeout() {
        return 300;
    }

    public boolean hasPermanentMenuKey() {
        return false;
    }

    public boolean shouldShowMenuShortcutsWhenKeyboardPresent() {
        return true;
    }
}
