package java.io;

public class SyncFailedException extends IOException {
    public SyncFailedException(String desc) {
        super(desc);
    }
}
