package android.hardware;

import android.graphics.Rect;

public class Camera {
    public static class Area {
        public Rect rect;
        public int weight;

        public Area(Rect rect, int weight) {
            this.rect = rect;
            this.weight = weight;
        }
    }

    public static class CameraInfo {
        public static final int CAMERA_FACING_BACK = 0;
        public static final int CAMERA_FACING_FRONT = 1;
        public int facing;
        public int orientation;
    }

    public static int getNumberOfCameras() {
        return 0;
    }

    public static Camera open(int cameraId) {
        throw new RuntimeException("Fail to connect to camera service");
    }
}
