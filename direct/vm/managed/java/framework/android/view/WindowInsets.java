package android.view;

import android.graphics.Insets;
import android.graphics.Rect;

/**
 * Window insets. The runner's windows have no system bars, cutouts or soft
 * keyboard, so the insets it dispatches are zero; applications may still
 * create and consume their own.
 */
public final class WindowInsets {
    public static final WindowInsets CONSUMED = new WindowInsets(0, 0, 0, 0, true);

    private final int mLeft;
    private final int mTop;
    private final int mRight;
    private final int mBottom;
    private final boolean mConsumed;

    private WindowInsets(int left, int top, int right, int bottom, boolean consumed) {
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        mConsumed = consumed;
    }

    public WindowInsets(WindowInsets src) {
        this(src.mLeft, src.mTop, src.mRight, src.mBottom, src.mConsumed);
    }

    static WindowInsets none() {
        return new WindowInsets(0, 0, 0, 0, false);
    }

    public int getSystemWindowInsetLeft() {
        return mLeft;
    }

    public int getSystemWindowInsetTop() {
        return mTop;
    }

    public int getSystemWindowInsetRight() {
        return mRight;
    }

    public int getSystemWindowInsetBottom() {
        return mBottom;
    }

    public Insets getSystemWindowInsets() {
        return Insets.of(mLeft, mTop, mRight, mBottom);
    }

    public Insets getInsets(int typeMask) {
        return Insets.of(mLeft, mTop, mRight, mBottom);
    }

    public Insets getInsetsIgnoringVisibility(int typeMask) {
        return getInsets(typeMask);
    }

    public boolean isVisible(int typeMask) {
        return false;
    }

    public int getStableInsetLeft() {
        return 0;
    }

    public int getStableInsetTop() {
        return 0;
    }

    public int getStableInsetRight() {
        return 0;
    }

    public int getStableInsetBottom() {
        return 0;
    }

    public boolean hasSystemWindowInsets() {
        return mLeft != 0 || mTop != 0 || mRight != 0 || mBottom != 0;
    }

    public boolean hasInsets() {
        return hasSystemWindowInsets();
    }

    public boolean isConsumed() {
        return mConsumed;
    }

    public boolean isRound() {
        return false;
    }

    public DisplayCutout getDisplayCutout() {
        return null;
    }

    public WindowInsets consumeSystemWindowInsets() {
        return new WindowInsets(0, 0, 0, 0, true);
    }

    public WindowInsets consumeStableInsets() {
        return this;
    }

    public WindowInsets consumeDisplayCutout() {
        return this;
    }

    public WindowInsets replaceSystemWindowInsets(int left, int top, int right, int bottom) {
        return new WindowInsets(left, top, right, bottom, false);
    }

    public WindowInsets replaceSystemWindowInsets(Rect r) {
        return replaceSystemWindowInsets(r.left, r.top, r.right, r.bottom);
    }

    public WindowInsets inset(int left, int top, int right, int bottom) {
        return new WindowInsets(Math.max(0, mLeft - left), Math.max(0, mTop - top), Math.max(0, mRight - right),
                Math.max(0, mBottom - bottom), mConsumed);
    }

    /** Inset type masks (WindowInsets.Type). */
    public static final class Type {
        private Type() {}

        public static int statusBars() {
            return 1;
        }

        public static int navigationBars() {
            return 1 << 1;
        }

        public static int captionBar() {
            return 1 << 2;
        }

        public static int ime() {
            return 1 << 3;
        }

        public static int systemGestures() {
            return 1 << 4;
        }

        public static int mandatorySystemGestures() {
            return 1 << 5;
        }

        public static int tappableElement() {
            return 1 << 6;
        }

        public static int displayCutout() {
            return 1 << 7;
        }

        public static int systemBars() {
            return statusBars() | navigationBars() | captionBar();
        }
    }

    public static final class Builder {
        private Insets mInsets = Insets.NONE;

        public Builder() {}

        public Builder(WindowInsets insets) {
            mInsets = insets.getSystemWindowInsets();
        }

        public Builder setSystemWindowInsets(Insets insets) {
            mInsets = insets;
            return this;
        }

        public Builder setInsets(int typeMask, Insets insets) {
            mInsets = insets;
            return this;
        }

        public WindowInsets build() {
            return new WindowInsets(mInsets.left, mInsets.top, mInsets.right, mInsets.bottom, false);
        }
    }

    @Override
    public String toString() {
        return "WindowInsets{systemWindowInsets=" + getSystemWindowInsets() + "}";
    }
}
