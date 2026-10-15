package android.os;

/** Process identity of the guest. IDs are the guest's, never the host's (spec 11). */
public class Process {
    public static final int SYSTEM_UID = 1000;
    public static final int FIRST_APPLICATION_UID = 10000;

    /** The single application uid of this desktop profile. */
    static final int APP_UID = 10001;

    public static int myPid() {
        return nativeMyPid();
    }

    public static int myTid() {
        return nativeMyTid();
    }

    public static int myUid() {
        return APP_UID;
    }

    public static UserHandle myUserHandle() {
        return UserHandle.OWNER;
    }

    public static final int THREAD_PRIORITY_DEFAULT = 0;
    public static final int THREAD_PRIORITY_LOWEST = 19;
    public static final int THREAD_PRIORITY_BACKGROUND = 10;
    public static final int THREAD_PRIORITY_FOREGROUND = -2;
    public static final int THREAD_PRIORITY_DISPLAY = -4;
    public static final int THREAD_PRIORITY_URGENT_DISPLAY = -8;
    public static final int THREAD_PRIORITY_AUDIO = -16;
    public static final int THREAD_PRIORITY_URGENT_AUDIO = -19;

    /** Host scheduling decides; priorities are accepted and ignored. */
    public static void setThreadPriority(int tid, int priority) {}

    public static void setThreadPriority(int priority) {}

    public static int getThreadPriority(int tid) {
        return THREAD_PRIORITY_DEFAULT;
    }

    public static void killProcess(int pid) {
        if (pid == myPid()) Runtime.getRuntime().halt(9);
    }

    private static native int nativeMyPid();

    private static native int nativeMyTid();
}
