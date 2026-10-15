/*
 * Core library for the androitainer DEX runtime (ADR 0004). Written for this
 * runtime; not derived from OpenJDK or AOSP libcore.
 */
package java.lang;

public class Object {
    public Object() {}

    public final native Class<?> getClass();

    /** Identity hash, stable for the object's lifetime. */
    public native int hashCode();

    public boolean equals(Object obj) {
        return this == obj;
    }

    /**
     * Shallow copy. Throws CloneNotSupportedException unless the class implements
     * Cloneable (arrays always do).
     */
    protected native Object clone() throws CloneNotSupportedException;

    public String toString() {
        return getClass().getName() + "@" + Integer.toHexString(hashCode());
    }

    public final native void notify();

    public final native void notifyAll();

    public final void wait() throws InterruptedException {
        wait(0L, 0);
    }

    public final void wait(long timeoutMillis) throws InterruptedException {
        wait(timeoutMillis, 0);
    }

    public final native void wait(long timeoutMillis, int nanos) throws InterruptedException;

    /** Finalizers are not run by this runtime (documented in ADR 0004). */
    @Deprecated
    protected void finalize() throws Throwable {}
}
