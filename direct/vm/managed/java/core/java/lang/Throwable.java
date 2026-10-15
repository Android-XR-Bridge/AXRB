package java.lang;

import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The runtime reads `detailMessage`, `cause` and `backtrace` directly when it
 * reports an uncaught exception, so their names and types are part of the
 * contract with the interpreter.
 */
public class Throwable implements Serializable {
    private String detailMessage;
    /** `this` means "not yet initialised", as in the JDK. */
    private Throwable cause = this;
    private StackTraceElement[] stackTrace;
    /** Opaque (method, pc) pairs captured by the runtime. */
    private transient Object backtrace;
    private List<Throwable> suppressedExceptions;

    public Throwable() {
        fillInStackTrace();
    }

    public Throwable(String message) {
        fillInStackTrace();
        detailMessage = message;
    }

    public Throwable(String message, Throwable cause) {
        fillInStackTrace();
        detailMessage = message;
        this.cause = cause;
    }

    public Throwable(Throwable cause) {
        fillInStackTrace();
        detailMessage = cause == null ? null : cause.toString();
        this.cause = cause;
    }

    protected Throwable(String message, Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
        if (writableStackTrace) fillInStackTrace();
        detailMessage = message;
        this.cause = cause;
    }

    public String getMessage() {
        return detailMessage;
    }

    public String getLocalizedMessage() {
        return getMessage();
    }

    public synchronized Throwable getCause() {
        return cause == this ? null : cause;
    }

    public synchronized Throwable initCause(Throwable cause) {
        if (this.cause != this) {
            throw new IllegalStateException("Can't overwrite cause with " + cause, this);
        }
        if (cause == this) throw new IllegalArgumentException("Self-causation not permitted", this);
        this.cause = cause;
        return this;
    }

    @Override
    public String toString() {
        String msg = getLocalizedMessage();
        String name = getClass().getName();
        return msg != null ? name + ": " + msg : name;
    }

    public synchronized Throwable fillInStackTrace() {
        backtrace = nativeFillInStackTrace();
        stackTrace = null;
        return this;
    }

    public StackTraceElement[] getStackTrace() {
        return getOurStackTrace().clone();
    }

    private synchronized StackTraceElement[] getOurStackTrace() {
        if (stackTrace == null) {
            stackTrace = backtrace == null ? new StackTraceElement[0] : nativeGetStackTrace(backtrace);
        }
        return stackTrace;
    }

    public void setStackTrace(StackTraceElement[] trace) {
        StackTraceElement[] copy = trace.clone();
        for (StackTraceElement e : copy) {
            if (e == null) throw new NullPointerException("stackTrace[i]");
        }
        synchronized (this) {
            stackTrace = copy;
        }
    }

    public final synchronized void addSuppressed(Throwable exception) {
        if (exception == this) throw new IllegalArgumentException("Self-suppression not permitted", exception);
        if (exception == null) throw new NullPointerException("Cannot suppress a null exception.");
        if (suppressedExceptions == null) suppressedExceptions = new ArrayList<Throwable>(1);
        suppressedExceptions.add(exception);
    }

    public final synchronized Throwable[] getSuppressed() {
        if (suppressedExceptions == null) return new Throwable[0];
        return suppressedExceptions.toArray(new Throwable[0]);
    }

    public void printStackTrace() {
        printStackTrace(System.err);
    }

    public void printStackTrace(PrintStream s) {
        StringBuilder sb = new StringBuilder();
        appendStackTrace(sb, "", null);
        s.print(sb.toString());
    }

    public void printStackTrace(PrintWriter s) {
        StringBuilder sb = new StringBuilder();
        appendStackTrace(sb, "", null);
        s.print(sb.toString());
    }

    private void appendStackTrace(StringBuilder sb, String prefix, StackTraceElement[] enclosing) {
        sb.append(prefix).append(this).append('\n');
        StackTraceElement[] trace = getOurStackTrace();
        int unique = trace.length;
        if (enclosing != null) {
            int j = enclosing.length - 1;
            while (unique > 0 && j >= 0 && trace[unique - 1].equals(enclosing[j])) {
                unique--;
                j--;
            }
        }
        for (int i = 0; i < unique; i++) sb.append("\tat ").append(trace[i]).append('\n');
        if (unique < trace.length) sb.append("\t... ").append(trace.length - unique).append(" more\n");
        for (Throwable t : getSuppressed()) t.appendStackTrace(sb, "Suppressed: ", trace);
        Throwable c = getCause();
        if (c != null && c != this) c.appendStackTrace(sb, "Caused by: ", trace);
    }

    private static native Object nativeFillInStackTrace();

    private static native StackTraceElement[] nativeGetStackTrace(Object backtrace);
}
