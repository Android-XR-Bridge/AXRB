package android.os;

public final class PowerManager {
    public static final int PARTIAL_WAKE_LOCK = 0x00000001;
    public static final int SCREEN_DIM_WAKE_LOCK = 0x00000006;
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 0x0000000a;
    public static final int FULL_WAKE_LOCK = 0x0000001a;
    public static final int ACQUIRE_CAUSES_WAKEUP = 0x10000000;
    public static final int ON_AFTER_RELEASE = 0x20000000;
    public static final int THERMAL_STATUS_NONE = 0;

    public PowerManager() {}

    public final class WakeLock {
        private int mCount;
        private boolean mRefCounted = true;

        WakeLock() {}

        public void acquire() {
            mCount++;
        }

        public void acquire(long timeout) {
            mCount++;
        }

        public void release() {
            if (mCount > 0) mCount--;
        }

        public void release(int flags) {
            release();
        }

        public boolean isHeld() {
            return mCount > 0;
        }

        public void setReferenceCounted(boolean value) {
            mRefCounted = value;
        }
    }

    public interface OnThermalStatusChangedListener {
        void onThermalStatusChanged(int status);
    }

    public WakeLock newWakeLock(int levelAndFlags, String tag) {
        return new WakeLock();
    }

    public boolean isScreenOn() {
        return true;
    }

    public boolean isInteractive() {
        return true;
    }

    public boolean isPowerSaveMode() {
        return false;
    }

    public boolean isSustainedPerformanceModeSupported() {
        return true;
    }

    public int getCurrentThermalStatus() {
        return THERMAL_STATUS_NONE;
    }

    public void addThermalStatusListener(OnThermalStatusChangedListener listener) {}

    /** The status never changes (THERMAL_STATUS_NONE), so listeners are never called. */
    public void addThermalStatusListener(java.util.concurrent.Executor executor, OnThermalStatusChangedListener listener) {}

    public void removeThermalStatusListener(OnThermalStatusChangedListener listener) {}

    public float getThermalHeadroom(int forecastSeconds) {
        return 0.5f;
    }
}
