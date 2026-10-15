package java.net;

public class UnknownServiceException extends java.io.IOException {
    public UnknownServiceException() {}

    public UnknownServiceException(String message) {
        super(message);
    }
}
