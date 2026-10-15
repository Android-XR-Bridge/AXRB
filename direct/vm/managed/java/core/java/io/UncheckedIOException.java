package java.io;

public class UncheckedIOException extends RuntimeException {
    public UncheckedIOException(String message, IOException cause) {
        super(message, cause);
    }

    public UncheckedIOException(IOException cause) {
        super(cause);
    }

    @Override
    public IOException getCause() {
        return (IOException) super.getCause();
    }
}
