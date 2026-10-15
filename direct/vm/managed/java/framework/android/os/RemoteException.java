package android.os;

public class RemoteException extends Exception {
    public RemoteException() {}

    public RemoteException(String message) {
        super(message);
    }

    public RuntimeException rethrowFromSystemServer() {
        return new RuntimeException(this);
    }

    public RuntimeException rethrowAsRuntimeException() {
        return new RuntimeException(this);
    }
}
