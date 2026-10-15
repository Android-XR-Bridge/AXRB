package android.util;

import java.io.PrintWriter;
import java.io.StringWriter;

public final class Log {
    public static final int VERBOSE = 2;
    public static final int DEBUG = 3;
    public static final int INFO = 4;
    public static final int WARN = 5;
    public static final int ERROR = 6;
    public static final int ASSERT = 7;

    private Log() {}

    public static int v(String tag, String msg) {
        return println(VERBOSE, tag, msg);
    }

    public static int v(String tag, String msg, Throwable tr) {
        return println(VERBOSE, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static int d(String tag, String msg) {
        return println(DEBUG, tag, msg);
    }

    public static int d(String tag, String msg, Throwable tr) {
        return println(DEBUG, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static int i(String tag, String msg) {
        return println(INFO, tag, msg);
    }

    public static int i(String tag, String msg, Throwable tr) {
        return println(INFO, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static int w(String tag, String msg) {
        return println(WARN, tag, msg);
    }

    public static int w(String tag, String msg, Throwable tr) {
        return println(WARN, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static int w(String tag, Throwable tr) {
        return println(WARN, tag, getStackTraceString(tr));
    }

    public static int e(String tag, String msg) {
        return println(ERROR, tag, msg);
    }

    public static int e(String tag, String msg, Throwable tr) {
        return println(ERROR, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static int wtf(String tag, String msg) {
        return println(ASSERT, tag, msg);
    }

    public static int wtf(String tag, Throwable tr) {
        return println(ASSERT, tag, getStackTraceString(tr));
    }

    public static int wtf(String tag, String msg, Throwable tr) {
        return println(ASSERT, tag, msg + '\n' + getStackTraceString(tr));
    }

    public static boolean isLoggable(String tag, int level) {
        return level >= INFO;
    }

    public static String getStackTraceString(Throwable tr) {
        if (tr == null) return "";
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        tr.printStackTrace(pw);
        pw.flush();
        return sw.toString();
    }

    public static int println(int priority, String tag, String msg) {
        if (msg == null) throw new NullPointerException("println needs a message");
        return println_native(0, priority, tag, msg);
    }

    private static native int println_native(int bufID, int priority, String tag, String msg);
}
