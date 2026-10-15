package android.os;

public class BatteryManager {
    public static final String EXTRA_LEVEL = "level";
    public static final String EXTRA_SCALE = "scale";
    public static final String EXTRA_STATUS = "status";
    public static final String EXTRA_PLUGGED = "plugged";
    public static final String EXTRA_TEMPERATURE = "temperature";
    public static final int BATTERY_STATUS_CHARGING = 2;
    public static final int BATTERY_STATUS_FULL = 5;
    public static final int BATTERY_PLUGGED_AC = 1;
    public static final int BATTERY_PROPERTY_CAPACITY = 4;
    public static final int BATTERY_PROPERTY_STATUS = 6;

    public BatteryManager() {}

    public int getIntProperty(int id) {
        if (id == BATTERY_PROPERTY_CAPACITY) return 100;
        if (id == BATTERY_PROPERTY_STATUS) return BATTERY_STATUS_FULL;
        return Integer.MIN_VALUE;
    }

    public long getLongProperty(int id) {
        return Long.MIN_VALUE;
    }

    public boolean isCharging() {
        return true;
    }
}
