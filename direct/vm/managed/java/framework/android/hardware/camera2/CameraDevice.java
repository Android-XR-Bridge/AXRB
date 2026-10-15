package android.hardware.camera2;

import android.os.Handler;
import java.util.List;

public abstract class CameraDevice implements AutoCloseable {
    public static final int TEMPLATE_PREVIEW = 1;
    public static final int TEMPLATE_STILL_CAPTURE = 2;
    public static final int TEMPLATE_RECORD = 3;

    public abstract static class StateCallback {
        public static final int ERROR_CAMERA_IN_USE = 1;
        public static final int ERROR_CAMERA_DISABLED = 3;
        public static final int ERROR_CAMERA_DEVICE = 4;

        public StateCallback() {}

        public abstract void onOpened(CameraDevice camera);

        public void onClosed(CameraDevice camera) {}

        public abstract void onDisconnected(CameraDevice camera);

        public abstract void onError(CameraDevice camera, int error);
    }

    public abstract String getId();

    public abstract CaptureRequest.Builder createCaptureRequest(int templateType) throws CameraAccessException;

    public abstract void createCaptureSession(List<android.view.Surface> outputs,
            CameraCaptureSession.StateCallback callback, Handler handler) throws CameraAccessException;

    public abstract void close();
}
