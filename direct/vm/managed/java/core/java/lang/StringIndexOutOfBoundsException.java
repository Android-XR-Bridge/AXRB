package java.lang;

public class StringIndexOutOfBoundsException extends IndexOutOfBoundsException {
    public StringIndexOutOfBoundsException() {
        super();
    }

    public StringIndexOutOfBoundsException(String message) {
        super(message);
    }

    public StringIndexOutOfBoundsException(int index) {
        super("String index out of range: " + index);
    }
}
