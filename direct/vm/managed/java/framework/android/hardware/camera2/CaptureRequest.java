package android.hardware.camera2;

public final class CaptureRequest {
    public static final class Key<T> {
        private final String mName;

        Key(String name) {
            mName = name;
        }

        public String getName() {
            return mName;
        }
    }

    public static final Key<Object> CONTROL_AE_TARGET_FPS_RANGE = new Key<Object>("android.control.aeTargetFpsRange");
    public static final Key<Integer> CONTROL_AF_MODE = new Key<Integer>("android.control.afMode");
    public static final Key<Object> CONTROL_AF_REGIONS = new Key<Object>("android.control.afRegions");
    public static final Key<Integer> CONTROL_AF_TRIGGER = new Key<Integer>("android.control.afTrigger");

    private Object mTag;

    CaptureRequest() {}

    public Object getTag() {
        return mTag;
    }

    public static final class Builder {
        private final CaptureRequest mRequest = new CaptureRequest();

        Builder() {}

        public void addTarget(android.view.Surface outputTarget) {}

        public void removeTarget(android.view.Surface outputTarget) {}

        public <T> void set(Key<T> key, T value) {}

        public void setTag(Object tag) {
            mRequest.mTag = tag;
        }

        public CaptureRequest build() {
            return mRequest;
        }
    }
}
