package android.os;

public final class UserHandle {
    public static final UserHandle OWNER = new UserHandle(0);
    private final int handle;

    private UserHandle(int h) {
        handle = h;
    }

    public int getIdentifier() {
        return handle;
    }

    @Override
    public String toString() {
        return "UserHandle{" + handle + "}";
    }
}
