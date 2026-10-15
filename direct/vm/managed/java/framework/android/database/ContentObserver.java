package android.database;

import android.net.Uri;
import android.os.Handler;

public abstract class ContentObserver {
    private final Handler mHandler;

    public ContentObserver(Handler handler) {
        mHandler = handler;
    }

    public boolean deliverSelfNotifications() {
        return false;
    }

    public void onChange(boolean selfChange) {}

    public void onChange(boolean selfChange, Uri uri) {
        onChange(selfChange);
    }

    public final void dispatchChange(final boolean selfChange, final Uri uri) {
        if (mHandler == null) {
            onChange(selfChange, uri);
        } else {
            mHandler.post(new Runnable() {
                public void run() {
                    onChange(selfChange, uri);
                }
            });
        }
    }

    @Deprecated
    public final void dispatchChange(boolean selfChange) {
        dispatchChange(selfChange, null);
    }
}
