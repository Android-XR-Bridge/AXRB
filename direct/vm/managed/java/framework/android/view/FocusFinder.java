package android.view;

import android.graphics.Rect;
import java.util.ArrayList;

/**
 * Finds the next view to take focus in a direction: nextFocus* ids first,
 * then tree order for FORWARD/BACKWARD (wrapping, as on Android) or the
 * nearest candidate in the direction by AOSP's beam and distance rules.
 */
public class FocusFinder {
    private static final FocusFinder sInstance = new FocusFinder();

    private final Rect mFocusedRect = new Rect();
    private final Rect mOtherRect = new Rect();
    private final Rect mBestCandidateRect = new Rect();

    public static FocusFinder getInstance() {
        return sInstance;
    }

    private FocusFinder() {}

    public final View findNextFocus(ViewGroup root, View focused, int direction) {
        if (focused != null) {
            View next = findUserSetNextFocus(root, focused, direction);
            if (next != null) return next;
        }
        ArrayList<View> focusables = new ArrayList<View>();
        root.addFocusables(focusables, direction);
        if (focusables.isEmpty()) return null;
        return findNextFocus(root, focused, null, direction, focusables);
    }

    public View findNextFocusFromRect(ViewGroup root, Rect focusedRect, int direction) {
        ArrayList<View> focusables = new ArrayList<View>();
        root.addFocusables(focusables, direction);
        if (focusables.isEmpty()) return null;
        return findNextFocus(root, null, focusedRect, direction, focusables);
    }

    private View findUserSetNextFocus(ViewGroup root, View focused, int direction) {
        int id = focused.nextFocusIdFor(direction);
        if (id == View.NO_ID) return null;
        View candidate = root.findViewById(id);
        while (candidate != null && candidate != focused) {
            if (candidate.canTakeFocus() && (!candidate.isInTouchMode() || candidate.isFocusableInTouchMode())) {
                return candidate;
            }
            int nextId = candidate.nextFocusIdFor(direction);
            if (nextId == View.NO_ID) return null;
            candidate = root.findViewById(nextId);
        }
        return null;
    }

    private View findNextFocus(ViewGroup root, View focused, Rect focusedRect, int direction,
            ArrayList<View> focusables) {
        if (direction == View.FOCUS_FORWARD || direction == View.FOCUS_BACKWARD) {
            final int count = focusables.size();
            final int position = focused != null ? focusables.lastIndexOf(focused) : -1;
            if (direction == View.FOCUS_FORWARD) {
                if (position >= 0 && position + 1 < count) return focusables.get(position + 1);
                return focusables.get(0);
            }
            if (position > 0) return focusables.get(position - 1);
            return focusables.get(count - 1);
        }
        if (focused != null) {
            focused.getFocusedRect(mFocusedRect);
            root.offsetDescendantRectToMyCoords(focused, mFocusedRect);
        } else if (focusedRect != null) {
            mFocusedRect.set(focusedRect);
        } else {
            // Start from the edge the direction points away from.
            switch (direction) {
                case View.FOCUS_RIGHT:
                case View.FOCUS_DOWN:
                    mFocusedRect.set(root.getScrollX(), root.getScrollY(), root.getScrollX(), root.getScrollY());
                    break;
                default:
                    int r = root.getScrollX() + root.getWidth();
                    int b = root.getScrollY() + root.getHeight();
                    mFocusedRect.set(r, b, r, b);
                    break;
            }
        }
        return findNextFocusInAbsoluteDirection(focusables, root, focused, mFocusedRect, direction);
    }

    View findNextFocusInAbsoluteDirection(ArrayList<View> focusables, ViewGroup root, View focused,
            Rect focusedRect, int direction) {
        mBestCandidateRect.set(focusedRect);
        switch (direction) {
            case View.FOCUS_LEFT:
                mBestCandidateRect.offset(focusedRect.width() + 1, 0);
                break;
            case View.FOCUS_RIGHT:
                mBestCandidateRect.offset(-(focusedRect.width() + 1), 0);
                break;
            case View.FOCUS_UP:
                mBestCandidateRect.offset(0, focusedRect.height() + 1);
                break;
            case View.FOCUS_DOWN:
                mBestCandidateRect.offset(0, -(focusedRect.height() + 1));
                break;
        }
        View closest = null;
        for (int i = 0; i < focusables.size(); i++) {
            View focusable = focusables.get(i);
            if (focusable == focused || focusable == root) continue;
            focusable.getFocusedRect(mOtherRect);
            root.offsetDescendantRectToMyCoords(focusable, mOtherRect);
            if (isBetterCandidate(direction, focusedRect, mOtherRect, mBestCandidateRect)) {
                mBestCandidateRect.set(mOtherRect);
                closest = focusable;
            }
        }
        return closest;
    }

    boolean isBetterCandidate(int direction, Rect source, Rect rect1, Rect rect2) {
        if (!isCandidate(source, rect1, direction)) return false;
        if (!isCandidate(source, rect2, direction)) return true;
        if (beamBeats(direction, source, rect1, rect2)) return true;
        if (beamBeats(direction, source, rect2, rect1)) return false;
        return getWeightedDistanceFor(majorAxisDistance(direction, source, rect1),
                minorAxisDistance(direction, source, rect1)) < getWeightedDistanceFor(
                majorAxisDistance(direction, source, rect2), minorAxisDistance(direction, source, rect2));
    }

    boolean beamBeats(int direction, Rect source, Rect rect1, Rect rect2) {
        final boolean rect1InSrcBeam = beamsOverlap(direction, source, rect1);
        final boolean rect2InSrcBeam = beamsOverlap(direction, source, rect2);
        if (rect2InSrcBeam || !rect1InSrcBeam) return false;
        if (!isToDirectionOf(direction, source, rect2)) return true;
        if (direction == View.FOCUS_LEFT || direction == View.FOCUS_RIGHT) return true;
        return majorAxisDistance(direction, source, rect1) < majorAxisDistanceToFarEdge(direction, source, rect2);
    }

    static int getWeightedDistanceFor(long majorAxisDistance, long minorAxisDistance) {
        long d = 13 * majorAxisDistance * majorAxisDistance + minorAxisDistance * minorAxisDistance;
        return d > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) d;
    }

    boolean isCandidate(Rect srcRect, Rect destRect, int direction) {
        switch (direction) {
            case View.FOCUS_LEFT:
                return (srcRect.right > destRect.right || srcRect.left >= destRect.right)
                        && srcRect.left > destRect.left;
            case View.FOCUS_RIGHT:
                return (srcRect.left < destRect.left || srcRect.right <= destRect.left)
                        && srcRect.right < destRect.right;
            case View.FOCUS_UP:
                return (srcRect.bottom > destRect.bottom || srcRect.top >= destRect.bottom)
                        && srcRect.top > destRect.top;
            case View.FOCUS_DOWN:
                return (srcRect.top < destRect.top || srcRect.bottom <= destRect.top)
                        && srcRect.bottom < destRect.bottom;
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

    boolean beamsOverlap(int direction, Rect rect1, Rect rect2) {
        switch (direction) {
            case View.FOCUS_LEFT:
            case View.FOCUS_RIGHT:
                return (rect2.bottom > rect1.top) && (rect2.top < rect1.bottom);
            case View.FOCUS_UP:
            case View.FOCUS_DOWN:
                return (rect2.right > rect1.left) && (rect2.left < rect1.right);
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

    boolean isToDirectionOf(int direction, Rect src, Rect dest) {
        switch (direction) {
            case View.FOCUS_LEFT:
                return src.left >= dest.right;
            case View.FOCUS_RIGHT:
                return src.right <= dest.left;
            case View.FOCUS_UP:
                return src.top >= dest.bottom;
            case View.FOCUS_DOWN:
                return src.bottom <= dest.top;
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

    static int majorAxisDistance(int direction, Rect source, Rect dest) {
        return Math.max(0, majorAxisDistanceRaw(direction, source, dest));
    }

    static int majorAxisDistanceRaw(int direction, Rect source, Rect dest) {
        switch (direction) {
            case View.FOCUS_LEFT:
                return source.left - dest.right;
            case View.FOCUS_RIGHT:
                return dest.left - source.right;
            case View.FOCUS_UP:
                return source.top - dest.bottom;
            case View.FOCUS_DOWN:
                return dest.top - source.bottom;
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

    static int majorAxisDistanceToFarEdge(int direction, Rect source, Rect dest) {
        return Math.max(1, majorAxisDistanceToFarEdgeRaw(direction, source, dest));
    }

    static int majorAxisDistanceToFarEdgeRaw(int direction, Rect source, Rect dest) {
        switch (direction) {
            case View.FOCUS_LEFT:
                return source.left - dest.left;
            case View.FOCUS_RIGHT:
                return dest.right - source.right;
            case View.FOCUS_UP:
                return source.top - dest.top;
            case View.FOCUS_DOWN:
                return dest.bottom - source.bottom;
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

    static int minorAxisDistance(int direction, Rect source, Rect dest) {
        switch (direction) {
            case View.FOCUS_LEFT:
            case View.FOCUS_RIGHT:
                return Math.abs(((source.top + source.height() / 2) - ((dest.top + dest.height() / 2))));
            case View.FOCUS_UP:
            case View.FOCUS_DOWN:
                return Math.abs(((source.left + source.width() / 2) - ((dest.left + dest.width() / 2))));
        }
        throw new IllegalArgumentException("direction must be one of {FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }
}
