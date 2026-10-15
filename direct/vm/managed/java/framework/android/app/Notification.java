package android.app;

/** Notifications are never shown in this runtime. */
public class Notification {
    public static final int FLAG_ONGOING_EVENT = 0x2;
    public static final int FLAG_NO_CLEAR = 0x20;
    public static final int FLAG_FOREGROUND_SERVICE = 0x40;
    public static final int PRIORITY_DEFAULT = 0;
    public static final int PRIORITY_LOW = -1;
    public static final int PRIORITY_MIN = -2;
    public static final int PRIORITY_HIGH = 1;
    public static final int PRIORITY_MAX = 2;

    public int flags;
    public int defaults;
    public int icon;
    public long when;
    public CharSequence tickerText;

    public Notification() {}
}
