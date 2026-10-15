package android.hardware.camera2;

import android.content.Context;
import android.os.Handler;

/** A device without cameras. */
public final class CameraManager {
    public CameraManager(Context context) {}

    public String[] getCameraIdList() throws CameraAccessException {
        return new String[0];
    }

    public CameraCharacteristics getCameraCharacteristics(String cameraId) throws CameraAccessException {
        throw new IllegalArgumentException("camera " + cameraId + " does not exist");
    }

    public void openCamera(String cameraId, CameraDevice.StateCallback callback, Handler handler)
            throws CameraAccessException {
        throw new IllegalArgumentException("camera " + cameraId + " does not exist");
    }
}
