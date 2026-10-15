package android.view;

import android.content.Context;

public abstract class OrientationEventListener {
    public static final int ORIENTATION_UNKNOWN = -1;

    private boolean mEnabled;

    public OrientationEventListener(Context context) {
        this(context, 3);
    }

    public OrientationEventListener(Context context, int rate) {}

    public void enable() {
        mEnabled = true;
    }

    public void disable() {
        mEnabled = false;
    }

    /** No accelerometer: orientation changes are never reported. */
    public boolean canDetectOrientation() {
        return false;
    }

    public abstract void onOrientationChanged(int orientation);
}
