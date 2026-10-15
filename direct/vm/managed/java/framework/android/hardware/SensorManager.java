package android.hardware;

import android.os.Handler;
import java.util.ArrayList;
import java.util.List;

/** A device without motion sensors (the headset pose comes from OpenXR). */
public class SensorManager {
    public static final int SENSOR_DELAY_FASTEST = 0;
    public static final int SENSOR_DELAY_GAME = 1;
    public static final int SENSOR_DELAY_UI = 2;
    public static final int SENSOR_DELAY_NORMAL = 3;
    public static final int SENSOR_STATUS_ACCURACY_HIGH = 3;
    public static final float GRAVITY_EARTH = 9.80665f;

    public SensorManager() {}

    public Sensor getDefaultSensor(int type) {
        return null;
    }

    public Sensor getDefaultSensor(int type, boolean wakeUp) {
        return null;
    }

    public List<Sensor> getSensorList(int type) {
        return new ArrayList<Sensor>();
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs) {
        return false;
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs,
            Handler handler) {
        return false;
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int samplingPeriodUs,
            int maxReportLatencyUs) {
        return false;
    }

    public void unregisterListener(SensorEventListener listener) {}

    public void unregisterListener(SensorEventListener listener, Sensor sensor) {}

    public static void getRotationMatrixFromVector(float[] r, float[] rotationVector) {}

    /** There are no motion sensors, so there is never a valid gravity sample. */
    public static boolean getRotationMatrix(float[] r, float[] i, float[] gravity, float[] geomagnetic) {
        return false;
    }

    public static float[] getOrientation(float[] r, float[] values) {
        return values;
    }
}
