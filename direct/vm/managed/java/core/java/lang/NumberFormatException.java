package java.lang;

public class NumberFormatException extends IllegalArgumentException {
    public NumberFormatException() {
        super();
    }

    public NumberFormatException(String message) {
        super(message);
    }

    static NumberFormatException forInputString(String s) {
        return new NumberFormatException("For input string: \"" + s + "\"");
    }
}
