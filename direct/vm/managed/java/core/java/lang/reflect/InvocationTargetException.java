package java.lang.reflect;

public class InvocationTargetException extends ReflectiveOperationException {
    private final Throwable target;

    protected InvocationTargetException() {
        super((Throwable) null);
        target = null;
    }

    public InvocationTargetException(Throwable target) {
        super((Throwable) null);
        this.target = target;
    }

    public InvocationTargetException(Throwable target, String message) {
        super(message, null);
        this.target = target;
    }

    public Throwable getTargetException() {
        return target;
    }

    @Override
    public Throwable getCause() {
        return target;
    }
}
