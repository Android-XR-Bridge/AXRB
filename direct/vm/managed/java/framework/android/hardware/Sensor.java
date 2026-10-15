package android.hardware;

public final class Sensor {
    public static final int TYPE_ACCELEROMETER = 1;
    public static final int TYPE_MAGNETIC_FIELD = 2;
    public static final int TYPE_ORIENTATION = 3;
    public static final int TYPE_GYROSCOPE = 4;
    public static final int TYPE_LIGHT = 5;
    public static final int TYPE_PRESSURE = 6;
    public static final int TYPE_PROXIMITY = 8;
    public static final int TYPE_GRAVITY = 9;
    public static final int TYPE_LINEAR_ACCELERATION = 10;
    public static final int TYPE_ROTATION_VECTOR = 11;
    public static final int TYPE_RELATIVE_HUMIDITY = 12;
    public static final int TYPE_AMBIENT_TEMPERATURE = 13;
    public static final int TYPE_GAME_ROTATION_VECTOR = 15;
    public static final int TYPE_STEP_COUNTER = 19;
    public static final int TYPE_ALL = -1;

    private final int mType;
    private final String mName;

    Sensor(int type, String name) {
        mType = type;
        mName = name;
    }

    public int getType() {
        return mType;
    }

    public String getName() {
        return mName;
    }

    public String getVendor() {
        return "AXRB";
    }

    public int getVersion() {
        return 1;
    }

    public float getMaximumRange() {
        return 0;
    }

    public float getResolution() {
        return 0;
    }

    public int getMinDelay() {
        return 0;
    }
}
