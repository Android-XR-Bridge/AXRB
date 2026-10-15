package android.util;

/** Thrown when a lifecycle override does not call through to super (hidden on Android). */
public final class SuperNotCalledException extends AndroidRuntimeException {
    public SuperNotCalledException(String msg) {
        super(msg);
    }
}
