package java.lang.invoke;

/** See MethodHandle: present for compilation only. */
public abstract class CallSite {
    CallSite() {}

    public abstract MethodHandle getTarget();
}
