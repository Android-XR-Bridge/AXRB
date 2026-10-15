package android.view;

public class HapticFeedbackConstants {
    private HapticFeedbackConstants() {}

    public static final int LONG_PRESS = 0;
    public static final int VIRTUAL_KEY = 1;
    public static final int KEYBOARD_TAP = 3;
    public static final int CLOCK_TICK = 4;
    public static final int CONTEXT_CLICK = 6;
    public static final int KEYBOARD_PRESS = KEYBOARD_TAP;
    public static final int KEYBOARD_RELEASE = 7;
    public static final int VIRTUAL_KEY_RELEASE = 8;
    public static final int TEXT_HANDLE_MOVE = 9;
    public static final int CONFIRM = 16;
    public static final int REJECT = 17;
    public static final int FLAG_IGNORE_VIEW_SETTING = 0x0001;
    @Deprecated
    public static final int FLAG_IGNORE_GLOBAL_SETTING = 0x0002;
}
