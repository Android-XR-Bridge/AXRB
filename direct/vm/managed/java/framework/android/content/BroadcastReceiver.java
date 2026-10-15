package android.content;

public abstract class BroadcastReceiver {
    public BroadcastReceiver() {}

    public abstract void onReceive(Context context, Intent intent);

    public final boolean isInitialStickyBroadcast() {
        return false;
    }

    public final PendingResult goAsync() {
        return new PendingResult();
    }

    public final void setResultCode(int code) {}

    public final int getResultCode() {
        return 0;
    }

    public final void abortBroadcast() {}

    public static class PendingResult {
        public final void finish() {}
    }
}
