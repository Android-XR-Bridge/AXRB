package android.hardware.camera2;

import android.os.Handler;

public abstract class CameraCaptureSession implements AutoCloseable {
    public abstract static class StateCallback {
        public StateCallback() {}

        public abstract void onConfigured(CameraCaptureSession session);

        public abstract void onConfigureFailed(CameraCaptureSession session);
    }

    public abstract static class CaptureCallback {
        public CaptureCallback() {}

        public void onCaptureStarted(CameraCaptureSession session, CaptureRequest request, long timestamp,
                long frameNumber) {}

        public void onCaptureCompleted(CameraCaptureSession session, CaptureRequest request,
                TotalCaptureResult result) {}

        public void onCaptureFailed(CameraCaptureSession session, CaptureRequest request, CaptureFailure failure) {}
    }

    public abstract int capture(CaptureRequest request, CaptureCallback listener, Handler handler)
            throws CameraAccessException;

    public abstract int setRepeatingRequest(CaptureRequest request, CaptureCallback listener, Handler handler)
            throws CameraAccessException;

    public abstract void stopRepeating() throws CameraAccessException;

    public abstract void abortCaptures() throws CameraAccessException;

    public abstract void close();
}
