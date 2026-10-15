package java.net;

public class ConnectException extends SocketException {
    public ConnectException() {}

    public ConnectException(String message) {
        super(message);
    }
}
