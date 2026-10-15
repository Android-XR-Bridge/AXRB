package android.media;

public final class AudioDeviceInfo {
    public static final int TYPE_BUILTIN_SPEAKER = 2;
    public static final int TYPE_WIRED_HEADSET = 3;
    public static final int TYPE_BLUETOOTH_SCO = 7;

    AudioDeviceInfo() {}

    public int getType() {
        return TYPE_BUILTIN_SPEAKER;
    }

    public int getId() {
        return 1;
    }
}
