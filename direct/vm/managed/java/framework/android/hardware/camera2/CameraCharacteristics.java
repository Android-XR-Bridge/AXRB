package android.hardware.camera2;

public final class CameraCharacteristics {
    public static final class Key<T> {
        private final String mName;

        public Key(String name, Class<T> type) {
            mName = name;
        }

        Key(String name) {
            mName = name;
        }

        public String getName() {
            return mName;
        }
    }

    public static final Key<Integer> LENS_FACING = new Key<Integer>("android.lens.facing");
    public static final Key<Integer> SENSOR_ORIENTATION = new Key<Integer>("android.sensor.orientation");
    public static final Key<Integer> INFO_SUPPORTED_HARDWARE_LEVEL = new Key<Integer>("android.info.supportedHardwareLevel");
    public static final Key<Integer> CONTROL_MAX_REGIONS_AF = new Key<Integer>("android.control.maxRegionsAf");
    public static final Key<float[]> LENS_INFO_AVAILABLE_FOCAL_LENGTHS = new Key<float[]>("android.lens.info.availableFocalLengths");
    public static final Key<Object> SCALER_STREAM_CONFIGURATION_MAP = new Key<Object>("android.scaler.streamConfigurationMap");
    public static final Key<Object> SENSOR_INFO_ACTIVE_ARRAY_SIZE = new Key<Object>("android.sensor.info.activeArraySize");
    public static final Key<Object> SENSOR_INFO_PHYSICAL_SIZE = new Key<Object>("android.sensor.info.physicalSize");
    public static final Key<Object> CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES = new Key<Object>("android.control.aeAvailableTargetFpsRanges");

    CameraCharacteristics() {}

    public <T> T get(Key<T> key) {
        return null;
    }
}
