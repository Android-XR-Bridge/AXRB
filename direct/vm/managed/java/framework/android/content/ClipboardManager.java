package android.content;

import android.os.SystemClock;
import java.util.ArrayList;

/**
 * The clipboard. The host clipboard is deliberately not shared with the
 * guest (spec 11): this clipboard lives for the session, in this process.
 */
public class ClipboardManager extends android.text.ClipboardManager {
    public interface OnPrimaryClipChangedListener {
        void onPrimaryClipChanged();
    }

    private static ClipData sPrimaryClip;
    private static final ArrayList<OnPrimaryClipChangedListener> sListeners =
            new ArrayList<OnPrimaryClipChangedListener>();

    private final Context mContext;

    public ClipboardManager(Context context) {
        mContext = context;
    }

    public void setPrimaryClip(ClipData clip) {
        if (clip == null) throw new NullPointerException("clip");
        ClipData copy = new ClipData(clip);
        copy.getDescription().setTimestamp(SystemClock.uptimeMillis());
        synchronized (sListeners) {
            sPrimaryClip = copy;
        }
        reportPrimaryClipChanged();
    }

    public void clearPrimaryClip() {
        synchronized (sListeners) {
            sPrimaryClip = null;
        }
        reportPrimaryClipChanged();
    }

    public ClipData getPrimaryClip() {
        synchronized (sListeners) {
            return sPrimaryClip != null ? new ClipData(sPrimaryClip) : null;
        }
    }

    public ClipDescription getPrimaryClipDescription() {
        synchronized (sListeners) {
            return sPrimaryClip != null ? new ClipDescription(sPrimaryClip.getDescription()) : null;
        }
    }

    public boolean hasPrimaryClip() {
        synchronized (sListeners) {
            return sPrimaryClip != null;
        }
    }

    public void addPrimaryClipChangedListener(OnPrimaryClipChangedListener what) {
        synchronized (sListeners) {
            sListeners.add(what);
        }
    }

    public void removePrimaryClipChangedListener(OnPrimaryClipChangedListener what) {
        synchronized (sListeners) {
            sListeners.remove(what);
        }
    }

    private void reportPrimaryClipChanged() {
        OnPrimaryClipChangedListener[] listeners;
        synchronized (sListeners) {
            listeners = sListeners.toArray(new OnPrimaryClipChangedListener[0]);
        }
        for (OnPrimaryClipChangedListener l : listeners) l.onPrimaryClipChanged();
    }

    @Override
    @Deprecated
    public CharSequence getText() {
        ClipData clip = getPrimaryClip();
        if (clip != null && clip.getItemCount() > 0) return clip.getItemAt(0).coerceToText(mContext);
        return null;
    }

    @Override
    @Deprecated
    public void setText(CharSequence text) {
        setPrimaryClip(ClipData.newPlainText(null, text));
    }

    @Override
    @Deprecated
    public boolean hasText() {
        ClipData clip = getPrimaryClip();
        return clip != null && clip.getItemCount() > 0 && clip.getItemAt(0).getText() != null;
    }
}
