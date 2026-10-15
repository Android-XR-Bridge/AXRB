package java.net;

public class SocketTimeoutException extends java.io.InterruptedIOException {
    public SocketTimeoutException() {}

    public SocketTimeoutException(String message) {
        super(message);
    }
}
