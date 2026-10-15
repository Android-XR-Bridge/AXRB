package java.lang;

public class ExceptionInInitializerError extends LinkageError {
    public ExceptionInInitializerError() {
        super();
    }

    public ExceptionInInitializerError(String message) {
        super(message);
    }

    public ExceptionInInitializerError(Throwable thrown) {
        super(null, thrown);
    }

    public Throwable getException() {
        return getCause();
    }
}
