package android.hardware.camera2;

public class CameraAccessException extends android.util.AndroidException {
    public static final int CAMERA_DISABLED = 1;
    public static final int CAMERA_DISCONNECTED = 2;
    public static final int CAMERA_ERROR = 3;

    private final int mReason;

    public CameraAccessException(int problem) {
        super("camera access error " + problem);
        mReason = problem;
    }

    public CameraAccessException(int problem, String message) {
        super(message);
        mReason = problem;
    }

    public final int getReason() {
        return mReason;
    }
}
