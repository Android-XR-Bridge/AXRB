package android.view;

import android.content.ClipData;
import android.content.ClipDescription;

/** A drag-and-drop event (the host starts no drags; the type exists for the API). */
public class DragEvent {
    public static final int ACTION_DRAG_STARTED = 1;
    public static final int ACTION_DRAG_LOCATION = 2;
    public static final int ACTION_DROP = 3;
    public static final int ACTION_DRAG_ENDED = 4;
    public static final int ACTION_DRAG_ENTERED = 5;
    public static final int ACTION_DRAG_EXITED = 6;

    int mAction;
    float mX;
    float mY;
    ClipDescription mClipDescription;
    ClipData mClipData;
    Object mLocalState;
    boolean mDragResult;

    private DragEvent() {}

    public int getAction() {
        return mAction;
    }

    public float getX() {
        return mX;
    }

    public float getY() {
        return mY;
    }

    public ClipData getClipData() {
        return mClipData;
    }

    public ClipDescription getClipDescription() {
        return mClipDescription;
    }

    public Object getLocalState() {
        return mLocalState;
    }

    public boolean getResult() {
        return mDragResult;
    }

    @Override
    public String toString() {
        return "DragEvent{action=" + mAction + " @ (" + mX + ", " + mY + ") result=" + mDragResult + "}";
    }
}
