package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.ViewGroup;

/**
 * Lays children out like a FrameLayout; the alignment rules applications use
 * for full-screen overlays (ALIGN_PARENT_*, CENTER_*) map onto gravity.
 */
public class RelativeLayout extends FrameLayout {
    public static final int TRUE = -1;
    public static final int LEFT_OF = 0;
    public static final int RIGHT_OF = 1;
    public static final int ABOVE = 2;
    public static final int BELOW = 3;
    public static final int ALIGN_BASELINE = 4;
    public static final int ALIGN_LEFT = 5;
    public static final int ALIGN_TOP = 6;
    public static final int ALIGN_RIGHT = 7;
    public static final int ALIGN_BOTTOM = 8;
    public static final int ALIGN_PARENT_LEFT = 9;
    public static final int ALIGN_PARENT_TOP = 10;
    public static final int ALIGN_PARENT_RIGHT = 11;
    public static final int ALIGN_PARENT_BOTTOM = 12;
    public static final int CENTER_IN_PARENT = 13;
    public static final int CENTER_HORIZONTAL = 14;
    public static final int CENTER_VERTICAL = 15;
    public static final int START_OF = 16;
    public static final int END_OF = 17;
    public static final int ALIGN_START = 18;
    public static final int ALIGN_END = 19;
    public static final int ALIGN_PARENT_START = 20;
    public static final int ALIGN_PARENT_END = 21;

    public RelativeLayout(Context context) {
        super(context);
    }

    public RelativeLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public RelativeLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public static class LayoutParams extends FrameLayout.LayoutParams {
        private final int[] mRules = new int[22];

        public LayoutParams(int w, int h) {
            super(w, h);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }

        public void addRule(int verb) {
            addRule(verb, TRUE);
        }

        public void addRule(int verb, int subject) {
            mRules[verb] = subject;
            updateGravity();
        }

        public void removeRule(int verb) {
            mRules[verb] = 0;
            updateGravity();
        }

        public int getRule(int verb) {
            return mRules[verb];
        }

        public int[] getRules() {
            return mRules;
        }

        private void updateGravity() {
            int g = 0;
            if (mRules[CENTER_IN_PARENT] != 0) g |= android.view.Gravity.CENTER;
            if (mRules[CENTER_HORIZONTAL] != 0) g |= android.view.Gravity.CENTER_HORIZONTAL;
            if (mRules[CENTER_VERTICAL] != 0) g |= android.view.Gravity.CENTER_VERTICAL;
            if (mRules[ALIGN_PARENT_LEFT] != 0 || mRules[ALIGN_PARENT_START] != 0) g |= android.view.Gravity.LEFT;
            if (mRules[ALIGN_PARENT_RIGHT] != 0 || mRules[ALIGN_PARENT_END] != 0) g |= android.view.Gravity.RIGHT;
            if (mRules[ALIGN_PARENT_TOP] != 0) g |= android.view.Gravity.TOP;
            if (mRules[ALIGN_PARENT_BOTTOM] != 0) g |= android.view.Gravity.BOTTOM;
            gravity = g == 0 ? -1 : g;
        }
    }
}
