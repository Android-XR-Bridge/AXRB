package android.view;

import android.graphics.Rect;

/** Routes touches in a larger area of a parent to a smaller delegate view. */
public class TouchDelegate {
    public static final int ABOVE = 1;
    public static final int BELOW = 2;
    public static final int TO_LEFT = 4;
    public static final int TO_RIGHT = 8;

    private final View mDelegateView;
    private final Rect mBounds;
    private final Rect mSlopBounds;
    private boolean mDelegateTargeted;
    private final int mSlop;

    public TouchDelegate(Rect bounds, View delegateView) {
        mBounds = bounds;
        mSlop = ViewConfiguration.get(delegateView.getContext()).getScaledTouchSlop();
        mSlopBounds = new Rect(bounds);
        mSlopBounds.inset(-mSlop, -mSlop);
        mDelegateView = delegateView;
    }

    public boolean onTouchEvent(MotionEvent event) {
        int x = (int) event.getX();
        int y = (int) event.getY();
        boolean sendToDelegate = false;
        boolean hit = true;
        boolean handled = false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                mDelegateTargeted = mBounds.contains(x, y);
                sendToDelegate = mDelegateTargeted;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_MOVE:
                sendToDelegate = mDelegateTargeted;
                if (sendToDelegate && !mSlopBounds.contains(x, y)) hit = false;
                break;
            case MotionEvent.ACTION_CANCEL:
                sendToDelegate = mDelegateTargeted;
                mDelegateTargeted = false;
                break;
        }
        if (sendToDelegate) {
            final View delegateView = mDelegateView;
            MotionEvent copy = MotionEvent.obtain(event);
            if (hit) {
                copy.setLocation(delegateView.getWidth() / 2f, delegateView.getHeight() / 2f);
            } else {
                int slop = mSlop;
                copy.setLocation(-(slop * 2), -(slop * 2));
            }
            handled = delegateView.dispatchTouchEvent(copy);
        }
        return handled;
    }
}
