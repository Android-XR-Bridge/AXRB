package android.hardware.camera2;

public class CaptureFailure {
    public static final int REASON_ERROR = 0;

    CaptureFailure() {}

    public int getReason() {
        return REASON_ERROR;
    }
}
