package android.view;

/** Input device descriptions. The desktop host has one keyboard and one pointer. */
public final class InputDevice {
    public static final int SOURCE_CLASS_MASK = 0x000000ff;
    public static final int SOURCE_CLASS_NONE = 0x00000000;
    public static final int SOURCE_CLASS_BUTTON = 0x00000001;
    public static final int SOURCE_CLASS_POINTER = 0x00000002;
    public static final int SOURCE_CLASS_TRACKBALL = 0x00000004;
    public static final int SOURCE_CLASS_POSITION = 0x00000008;
    public static final int SOURCE_CLASS_JOYSTICK = 0x00000010;
    public static final int SOURCE_UNKNOWN = 0x00000000;
    public static final int SOURCE_KEYBOARD = 0x00000100 | SOURCE_CLASS_BUTTON;
    public static final int SOURCE_DPAD = 0x00000200 | SOURCE_CLASS_BUTTON;
    public static final int SOURCE_TOUCHSCREEN = 0x00001000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_MOUSE = 0x00002000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_STYLUS = 0x00004000 | SOURCE_CLASS_POINTER;
    public static final int SOURCE_TOUCHPAD = 0x00100000 | SOURCE_CLASS_POSITION;
    public static final int SOURCE_ANY = 0xffffff00;

    public static final int KEYBOARD_TYPE_NONE = 0;
    public static final int KEYBOARD_TYPE_NON_ALPHABETIC = 1;
    public static final int KEYBOARD_TYPE_ALPHABETIC = 2;

    private final int mId;
    private final int mSources;
    private final String mName;

    private InputDevice(int id, int sources, String name) {
        mId = id;
        mSources = sources;
        mName = name;
    }

    private static final InputDevice KEYBOARD = new InputDevice(-1, SOURCE_KEYBOARD, "Virtual");
    private static final InputDevice POINTER = new InputDevice(0, SOURCE_TOUCHSCREEN | SOURCE_MOUSE, "Pointer");

    public static InputDevice getDevice(int id) {
        if (id == -1) return KEYBOARD;
        if (id == 0) return POINTER;
        return null;
    }

    public static int[] getDeviceIds() {
        return new int[] {-1, 0};
    }

    public int getId() {
        return mId;
    }

    public String getName() {
        return mName;
    }

    public int getSources() {
        return mSources;
    }

    public boolean supportsSource(int source) {
        return (mSources & source) == source;
    }

    public int getVendorId() {
        return 0;
    }

    public int getProductId() {
        return 0;
    }

    public int getControllerNumber() {
        return 0;
    }

    public String getDescriptor() {
        return "axrb-input-" + getId();
    }

    public android.os.Vibrator getVibrator() {
        return new android.os.Vibrator();
    }

    public android.os.VibratorManager getVibratorManager() {
        return android.os.VibratorManager.getEmpty();
    }

    public int getKeyboardType() {
        return mId == -1 ? KEYBOARD_TYPE_ALPHABETIC : KEYBOARD_TYPE_NONE;
    }

    public boolean isVirtual() {
        return mId < 0;
    }

    public KeyCharacterMap getKeyCharacterMap() {
        return KeyCharacterMap.load(mId);
    }

    @Override
    public String toString() {
        return "Input Device " + mId + ": " + mName;
    }
}
