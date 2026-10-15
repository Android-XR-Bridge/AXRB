package android.view;

import android.os.SystemClock;

/**
 * A key event. Host keyboard input arrives with an Android key code for
 * non-character keys and with the typed character (getUnicodeChar) for
 * character keys; ASCII letters, digits and common punctuation also carry
 * their Android key code.
 */
public class KeyEvent extends InputEvent {
    public static final int KEYCODE_UNKNOWN = 0;
    public static final int KEYCODE_SOFT_LEFT = 1;
    public static final int KEYCODE_SOFT_RIGHT = 2;
    public static final int KEYCODE_HOME = 3;
    public static final int KEYCODE_BACK = 4;
    public static final int KEYCODE_CALL = 5;
    public static final int KEYCODE_ENDCALL = 6;
    public static final int KEYCODE_0 = 7;
    public static final int KEYCODE_1 = 8;
    public static final int KEYCODE_2 = 9;
    public static final int KEYCODE_3 = 10;
    public static final int KEYCODE_4 = 11;
    public static final int KEYCODE_5 = 12;
    public static final int KEYCODE_6 = 13;
    public static final int KEYCODE_7 = 14;
    public static final int KEYCODE_8 = 15;
    public static final int KEYCODE_9 = 16;
    public static final int KEYCODE_STAR = 17;
    public static final int KEYCODE_POUND = 18;
    public static final int KEYCODE_DPAD_UP = 19;
    public static final int KEYCODE_DPAD_DOWN = 20;
    public static final int KEYCODE_DPAD_LEFT = 21;
    public static final int KEYCODE_DPAD_RIGHT = 22;
    public static final int KEYCODE_DPAD_CENTER = 23;
    public static final int KEYCODE_VOLUME_UP = 24;
    public static final int KEYCODE_VOLUME_DOWN = 25;
    public static final int KEYCODE_POWER = 26;
    public static final int KEYCODE_CAMERA = 27;
    public static final int KEYCODE_CLEAR = 28;
    public static final int KEYCODE_A = 29;
    public static final int KEYCODE_B = 30;
    public static final int KEYCODE_C = 31;
    public static final int KEYCODE_D = 32;
    public static final int KEYCODE_E = 33;
    public static final int KEYCODE_F = 34;
    public static final int KEYCODE_G = 35;
    public static final int KEYCODE_H = 36;
    public static final int KEYCODE_I = 37;
    public static final int KEYCODE_J = 38;
    public static final int KEYCODE_K = 39;
    public static final int KEYCODE_L = 40;
    public static final int KEYCODE_M = 41;
    public static final int KEYCODE_N = 42;
    public static final int KEYCODE_O = 43;
    public static final int KEYCODE_P = 44;
    public static final int KEYCODE_Q = 45;
    public static final int KEYCODE_R = 46;
    public static final int KEYCODE_S = 47;
    public static final int KEYCODE_T = 48;
    public static final int KEYCODE_U = 49;
    public static final int KEYCODE_V = 50;
    public static final int KEYCODE_W = 51;
    public static final int KEYCODE_X = 52;
    public static final int KEYCODE_Y = 53;
    public static final int KEYCODE_Z = 54;
    public static final int KEYCODE_COMMA = 55;
    public static final int KEYCODE_PERIOD = 56;
    public static final int KEYCODE_ALT_LEFT = 57;
    public static final int KEYCODE_ALT_RIGHT = 58;
    public static final int KEYCODE_SHIFT_LEFT = 59;
    public static final int KEYCODE_SHIFT_RIGHT = 60;
    public static final int KEYCODE_TAB = 61;
    public static final int KEYCODE_SPACE = 62;
    public static final int KEYCODE_SYM = 63;
    public static final int KEYCODE_EXPLORER = 64;
    public static final int KEYCODE_ENVELOPE = 65;
    public static final int KEYCODE_ENTER = 66;
    public static final int KEYCODE_DEL = 67;
    public static final int KEYCODE_GRAVE = 68;
    public static final int KEYCODE_MINUS = 69;
    public static final int KEYCODE_EQUALS = 70;
    public static final int KEYCODE_LEFT_BRACKET = 71;
    public static final int KEYCODE_RIGHT_BRACKET = 72;
    public static final int KEYCODE_BACKSLASH = 73;
    public static final int KEYCODE_SEMICOLON = 74;
    public static final int KEYCODE_APOSTROPHE = 75;
    public static final int KEYCODE_SLASH = 76;
    public static final int KEYCODE_AT = 77;
    public static final int KEYCODE_NUM = 78;
    public static final int KEYCODE_HEADSETHOOK = 79;
    public static final int KEYCODE_FOCUS = 80;
    public static final int KEYCODE_PLUS = 81;
    public static final int KEYCODE_MENU = 82;
    public static final int KEYCODE_NOTIFICATION = 83;
    public static final int KEYCODE_SEARCH = 84;
    public static final int KEYCODE_MEDIA_PLAY_PAUSE = 85;
    public static final int KEYCODE_MEDIA_STOP = 86;
    public static final int KEYCODE_MEDIA_NEXT = 87;
    public static final int KEYCODE_MEDIA_PREVIOUS = 88;
    public static final int KEYCODE_MEDIA_REWIND = 89;
    public static final int KEYCODE_MEDIA_FAST_FORWARD = 90;
    public static final int KEYCODE_MUTE = 91;
    public static final int KEYCODE_PAGE_UP = 92;
    public static final int KEYCODE_PAGE_DOWN = 93;
    public static final int KEYCODE_ESCAPE = 111;
    public static final int KEYCODE_FORWARD_DEL = 112;
    public static final int KEYCODE_CTRL_LEFT = 113;
    public static final int KEYCODE_CTRL_RIGHT = 114;
    public static final int KEYCODE_CAPS_LOCK = 115;
    public static final int KEYCODE_SCROLL_LOCK = 116;
    public static final int KEYCODE_META_LEFT = 117;
    public static final int KEYCODE_META_RIGHT = 118;
    public static final int KEYCODE_FUNCTION = 119;
    public static final int KEYCODE_SYSRQ = 120;
    public static final int KEYCODE_BREAK = 121;
    public static final int KEYCODE_MOVE_HOME = 122;
    public static final int KEYCODE_MOVE_END = 123;
    public static final int KEYCODE_INSERT = 124;
    public static final int KEYCODE_FORWARD = 125;
    public static final int KEYCODE_MEDIA_PLAY = 126;
    public static final int KEYCODE_MEDIA_PAUSE = 127;
    public static final int KEYCODE_F1 = 131;
    public static final int KEYCODE_F12 = 142;
    public static final int KEYCODE_NUMPAD_ENTER = 160;
    public static final int KEYCODE_VOLUME_MUTE = 164;
    public static final int KEYCODE_APP_SWITCH = 187;
    public static final int KEYCODE_NAVIGATE_PREVIOUS = 260;
    public static final int KEYCODE_NAVIGATE_NEXT = 261;

    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    @Deprecated
    public static final int ACTION_MULTIPLE = 2;

    public static final int META_ALT_ON = 0x02;
    public static final int META_ALT_LEFT_ON = 0x10;
    public static final int META_ALT_RIGHT_ON = 0x20;
    public static final int META_SHIFT_ON = 0x1;
    public static final int META_SHIFT_LEFT_ON = 0x40;
    public static final int META_SHIFT_RIGHT_ON = 0x80;
    public static final int META_SYM_ON = 0x4;
    public static final int META_FUNCTION_ON = 0x8;
    public static final int META_CTRL_ON = 0x1000;
    public static final int META_CTRL_LEFT_ON = 0x2000;
    public static final int META_CTRL_RIGHT_ON = 0x4000;
    public static final int META_META_ON = 0x10000;
    public static final int META_META_LEFT_ON = 0x20000;
    public static final int META_META_RIGHT_ON = 0x40000;
    public static final int META_CAPS_LOCK_ON = 0x100000;
    public static final int META_NUM_LOCK_ON = 0x200000;
    public static final int META_SCROLL_LOCK_ON = 0x400000;
    public static final int META_SHIFT_MASK = META_SHIFT_ON | META_SHIFT_LEFT_ON | META_SHIFT_RIGHT_ON;
    public static final int META_ALT_MASK = META_ALT_ON | META_ALT_LEFT_ON | META_ALT_RIGHT_ON;
    public static final int META_CTRL_MASK = META_CTRL_ON | META_CTRL_LEFT_ON | META_CTRL_RIGHT_ON;
    public static final int META_META_MASK = META_META_ON | META_META_LEFT_ON | META_META_RIGHT_ON;

    public static final int FLAG_WOKE_HERE = 0x1;
    public static final int FLAG_SOFT_KEYBOARD = 0x2;
    public static final int FLAG_KEEP_TOUCH_MODE = 0x4;
    public static final int FLAG_FROM_SYSTEM = 0x8;
    public static final int FLAG_EDITOR_ACTION = 0x10;
    public static final int FLAG_CANCELED = 0x20;
    public static final int FLAG_VIRTUAL_HARD_KEY = 0x40;
    public static final int FLAG_LONG_PRESS = 0x80;
    public static final int FLAG_CANCELED_LONG_PRESS = 0x100;
    public static final int FLAG_TRACKING = 0x200;
    public static final int FLAG_FALLBACK = 0x400;

    private static final int MODIFIER_META_MASK = META_SHIFT_MASK | META_ALT_MASK | META_CTRL_MASK | META_META_MASK
            | META_SYM_ON | META_FUNCTION_ON;

    public interface Callback {
        boolean onKeyDown(int keyCode, KeyEvent event);

        boolean onKeyLongPress(int keyCode, KeyEvent event);

        boolean onKeyUp(int keyCode, KeyEvent event);

        boolean onKeyMultiple(int keyCode, int count, KeyEvent event);
    }

    /** Tracks the key a view received ACTION_DOWN for, so that it also gets the up. */
    public static class DispatcherState {
        private int mDownKeyCode;
        private Object mDownTarget;

        public void reset() {
            mDownKeyCode = 0;
            mDownTarget = null;
        }

        public void reset(Object target) {
            if (mDownTarget == target) reset();
        }

        public void startTracking(KeyEvent event, Object target) {
            if (event.getAction() != ACTION_DOWN) throw new IllegalArgumentException("Can only start tracking on a down event");
            mDownKeyCode = event.getKeyCode();
            mDownTarget = target;
        }

        public boolean isTracking(KeyEvent event) {
            return mDownKeyCode == event.getKeyCode();
        }

        public void handleUpEvent(KeyEvent event) {
            if (mDownKeyCode == event.getKeyCode()) {
                event.mFlags |= FLAG_TRACKING;
                mDownKeyCode = 0;
                mDownTarget = null;
            }
        }
    }

    private int mDeviceId;
    private int mSource = InputDevice.SOURCE_KEYBOARD;
    private int mMetaState;
    private int mAction;
    private int mKeyCode;
    private int mScanCode;
    private int mRepeatCount;
    int mFlags;
    private long mDownTime;
    private long mEventTime;
    private String mCharacters;
    private int mUnicode;

    public KeyEvent(int action, int code) {
        mAction = action;
        mKeyCode = code;
        mDownTime = mEventTime = SystemClock.uptimeMillis();
        mUnicode = defaultUnicode(code, 0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat) {
        mDownTime = downTime;
        mEventTime = eventTime;
        mAction = action;
        mKeyCode = code;
        mRepeatCount = repeat;
        mUnicode = defaultUnicode(code, 0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat, int metaState) {
        this(downTime, eventTime, action, code, repeat);
        mMetaState = metaState;
        mUnicode = defaultUnicode(code, metaState);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat, int metaState, int deviceId,
            int scancode) {
        this(downTime, eventTime, action, code, repeat, metaState);
        mDeviceId = deviceId;
        mScanCode = scancode;
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat, int metaState, int deviceId,
            int scancode, int flags) {
        this(downTime, eventTime, action, code, repeat, metaState, deviceId, scancode);
        mFlags = flags;
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat, int metaState, int deviceId,
            int scancode, int flags, int source) {
        this(downTime, eventTime, action, code, repeat, metaState, deviceId, scancode, flags);
        mSource = source;
    }

    public KeyEvent(long time, String characters, int deviceId, int flags) {
        mDownTime = mEventTime = time;
        mCharacters = characters;
        mAction = ACTION_MULTIPLE;
        mKeyCode = KEYCODE_UNKNOWN;
        mRepeatCount = 0;
        mDeviceId = deviceId;
        mFlags = flags;
    }

    public KeyEvent(KeyEvent origEvent) {
        mDownTime = origEvent.mDownTime;
        mEventTime = origEvent.mEventTime;
        mAction = origEvent.mAction;
        mKeyCode = origEvent.mKeyCode;
        mRepeatCount = origEvent.mRepeatCount;
        mMetaState = origEvent.mMetaState;
        mDeviceId = origEvent.mDeviceId;
        mSource = origEvent.mSource;
        mScanCode = origEvent.mScanCode;
        mFlags = origEvent.mFlags;
        mCharacters = origEvent.mCharacters;
        mUnicode = origEvent.mUnicode;
    }

    private KeyEvent(KeyEvent origEvent, int action) {
        this(origEvent);
        mAction = action;
    }

    /** A host key: the Android key code (or UNKNOWN) and the typed character (or 0). */
    static KeyEvent fromHost(int action, int keyCode, int unicode, int metaState, long time) {
        if (keyCode == KEYCODE_UNKNOWN && unicode != 0) keyCode = keyCodeForChar(unicode);
        KeyEvent e = new KeyEvent(time, time, action, keyCode, 0, metaState, -1, 0, 0, InputDevice.SOURCE_KEYBOARD);
        e.mUnicode = unicode != 0 ? unicode : defaultUnicode(keyCode, metaState);
        return e;
    }

    static int keyCodeForChar(int c) {
        if (c >= 'a' && c <= 'z') return KEYCODE_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return KEYCODE_A + (c - 'A');
        if (c >= '0' && c <= '9') return KEYCODE_0 + (c - '0');
        switch (c) {
            case ' ': return KEYCODE_SPACE;
            case ',': return KEYCODE_COMMA;
            case '.': return KEYCODE_PERIOD;
            case '-': return KEYCODE_MINUS;
            case '=': return KEYCODE_EQUALS;
            case '[': return KEYCODE_LEFT_BRACKET;
            case ']': return KEYCODE_RIGHT_BRACKET;
            case '\\': return KEYCODE_BACKSLASH;
            case ';': return KEYCODE_SEMICOLON;
            case '\'': return KEYCODE_APOSTROPHE;
            case '/': return KEYCODE_SLASH;
            case '@': return KEYCODE_AT;
            case '+': return KEYCODE_PLUS;
            case '*': return KEYCODE_STAR;
            case '#': return KEYCODE_POUND;
            case '`': return KEYCODE_GRAVE;
            default: return KEYCODE_UNKNOWN;
        }
    }

    private static int defaultUnicode(int keyCode, int meta) {
        boolean shift = (meta & META_SHIFT_MASK) != 0 ^ (meta & META_CAPS_LOCK_ON) != 0;
        if (keyCode >= KEYCODE_A && keyCode <= KEYCODE_Z) return (shift ? 'A' : 'a') + (keyCode - KEYCODE_A);
        if (keyCode >= KEYCODE_0 && keyCode <= KEYCODE_9) return '0' + (keyCode - KEYCODE_0);
        switch (keyCode) {
            case KEYCODE_SPACE: return ' ';
            case KEYCODE_ENTER: return '\n';
            case KEYCODE_TAB: return '\t';
            case KEYCODE_COMMA: return ',';
            case KEYCODE_PERIOD: return '.';
            case KEYCODE_MINUS: return '-';
            case KEYCODE_EQUALS: return '=';
            case KEYCODE_SLASH: return '/';
            case KEYCODE_AT: return '@';
            case KEYCODE_PLUS: return '+';
            case KEYCODE_STAR: return '*';
            case KEYCODE_POUND: return '#';
            case KEYCODE_SEMICOLON: return ';';
            case KEYCODE_APOSTROPHE: return '\'';
            default: return 0;
        }
    }

    public static KeyEvent obtain(long downTime, long eventTime, int action, int code, int repeat, int metaState,
            int deviceId, int scancode, int flags, int source, String characters) {
        KeyEvent ev = new KeyEvent(downTime, eventTime, action, code, repeat, metaState, deviceId, scancode, flags,
                source);
        ev.mCharacters = characters;
        return ev;
    }

    public static KeyEvent obtain(KeyEvent other) {
        return new KeyEvent(other);
    }

    public static KeyEvent changeTimeRepeat(KeyEvent event, long eventTime, int newRepeat) {
        KeyEvent ret = new KeyEvent(event);
        ret.mEventTime = eventTime;
        ret.mRepeatCount = newRepeat;
        return ret;
    }

    public static KeyEvent changeAction(KeyEvent event, int action) {
        return new KeyEvent(event, action);
    }

    public static KeyEvent changeFlags(KeyEvent event, int flags) {
        KeyEvent ret = new KeyEvent(event);
        ret.mFlags = flags;
        return ret;
    }

    public static int getMaxKeyCode() {
        return 288;
    }

    public static int getDeadChar(int accent, int c) {
        return 0;
    }

    public static boolean isModifierKey(int keyCode) {
        switch (keyCode) {
            case KEYCODE_SHIFT_LEFT:
            case KEYCODE_SHIFT_RIGHT:
            case KEYCODE_ALT_LEFT:
            case KEYCODE_ALT_RIGHT:
            case KEYCODE_CTRL_LEFT:
            case KEYCODE_CTRL_RIGHT:
            case KEYCODE_META_LEFT:
            case KEYCODE_META_RIGHT:
            case KEYCODE_SYM:
            case KEYCODE_NUM:
            case KEYCODE_FUNCTION:
                return true;
            default:
                return false;
        }
    }

    public static boolean isMediaSessionKey(int keyCode) {
        switch (keyCode) {
            case KEYCODE_MEDIA_PLAY:
            case KEYCODE_MEDIA_PAUSE:
            case KEYCODE_MEDIA_PLAY_PAUSE:
            case KEYCODE_MUTE:
            case KEYCODE_HEADSETHOOK:
            case KEYCODE_MEDIA_STOP:
            case KEYCODE_MEDIA_NEXT:
            case KEYCODE_MEDIA_PREVIOUS:
            case KEYCODE_MEDIA_REWIND:
            case KEYCODE_MEDIA_FAST_FORWARD:
                return true;
            default:
                return false;
        }
    }

    public static boolean isGamepadButton(int keyCode) {
        return keyCode >= 96 && keyCode <= 110;
    }

    public static int normalizeMetaState(int metaState) {
        if ((metaState & (META_SHIFT_LEFT_ON | META_SHIFT_RIGHT_ON)) != 0) metaState |= META_SHIFT_ON;
        if ((metaState & (META_ALT_LEFT_ON | META_ALT_RIGHT_ON)) != 0) metaState |= META_ALT_ON;
        if ((metaState & (META_CTRL_LEFT_ON | META_CTRL_RIGHT_ON)) != 0) metaState |= META_CTRL_ON;
        if ((metaState & (META_META_LEFT_ON | META_META_RIGHT_ON)) != 0) metaState |= META_META_ON;
        return metaState;
    }

    public static boolean metaStateHasNoModifiers(int metaState) {
        return (normalizeMetaState(metaState) & MODIFIER_META_MASK) == 0;
    }

    public static boolean metaStateHasModifiers(int metaState, int modifiers) {
        return (normalizeMetaState(metaState) & MODIFIER_META_MASK) == modifiers;
    }

    public final boolean hasNoModifiers() {
        return metaStateHasNoModifiers(mMetaState);
    }

    public final boolean hasModifiers(int modifiers) {
        return metaStateHasModifiers(mMetaState, modifiers);
    }

    public final boolean isSystem() {
        switch (mKeyCode) {
            case KEYCODE_MENU:
            case KEYCODE_SOFT_RIGHT:
            case KEYCODE_HOME:
            case KEYCODE_BACK:
            case KEYCODE_CALL:
            case KEYCODE_ENDCALL:
            case KEYCODE_VOLUME_UP:
            case KEYCODE_VOLUME_DOWN:
            case KEYCODE_VOLUME_MUTE:
            case KEYCODE_MUTE:
            case KEYCODE_POWER:
            case KEYCODE_HEADSETHOOK:
            case KEYCODE_CAMERA:
            case KEYCODE_FOCUS:
            case KEYCODE_SEARCH:
                return true;
            default:
                return false;
        }
    }

    public final boolean isCanceled() {
        return (mFlags & FLAG_CANCELED) != 0;
    }

    public final void startTracking() {
        mFlags |= FLAG_TRACKING;
    }

    public final boolean isTracking() {
        return (mFlags & FLAG_TRACKING) != 0;
    }

    public final boolean isLongPress() {
        return (mFlags & FLAG_LONG_PRESS) != 0;
    }

    public final int getAction() {
        return mAction;
    }

    public final int getKeyCode() {
        return mKeyCode;
    }

    public final String getCharacters() {
        return mCharacters;
    }

    public final int getScanCode() {
        return mScanCode;
    }

    public final int getRepeatCount() {
        return mRepeatCount;
    }

    public final int getMetaState() {
        return mMetaState;
    }

    public final int getModifiers() {
        return normalizeMetaState(mMetaState) & MODIFIER_META_MASK;
    }

    public final int getFlags() {
        return mFlags;
    }

    public final long getDownTime() {
        return mDownTime;
    }

    @Override
    public final long getEventTime() {
        return mEventTime;
    }

    @Override
    public final int getDeviceId() {
        return mDeviceId;
    }

    @Override
    public final int getSource() {
        return mSource;
    }

    @Override
    public final void setSource(int source) {
        mSource = source;
    }

    public final boolean isAltPressed() {
        return (mMetaState & META_ALT_ON) != 0;
    }

    public final boolean isShiftPressed() {
        return (mMetaState & META_SHIFT_ON) != 0;
    }

    public final boolean isSymPressed() {
        return (mMetaState & META_SYM_ON) != 0;
    }

    public final boolean isCtrlPressed() {
        return (mMetaState & META_CTRL_ON) != 0;
    }

    public final boolean isMetaPressed() {
        return (mMetaState & META_META_ON) != 0;
    }

    public final boolean isFunctionPressed() {
        return (mMetaState & META_FUNCTION_ON) != 0;
    }

    public final boolean isCapsLockOn() {
        return (mMetaState & META_CAPS_LOCK_ON) != 0;
    }

    public final boolean isNumLockOn() {
        return (mMetaState & META_NUM_LOCK_ON) != 0;
    }

    public final boolean isScrollLockOn() {
        return (mMetaState & META_SCROLL_LOCK_ON) != 0;
    }

    public int getUnicodeChar() {
        return mUnicode;
    }

    public int getUnicodeChar(int metaState) {
        if (metaState == mMetaState) return mUnicode;
        int u = defaultUnicode(mKeyCode, metaState);
        return u != 0 ? u : mUnicode;
    }

    public char getDisplayLabel() {
        int u = defaultUnicode(mKeyCode, META_SHIFT_ON);
        return (char) (u != 0 ? u : mUnicode);
    }

    public char getNumber() {
        if (mKeyCode >= KEYCODE_0 && mKeyCode <= KEYCODE_9) return (char) ('0' + mKeyCode - KEYCODE_0);
        return 0;
    }

    public boolean isPrintingKey() {
        return mUnicode != 0 && !Character.isISOControl(mUnicode);
    }

    public final KeyCharacterMap getKeyCharacterMap() {
        return KeyCharacterMap.load(mDeviceId);
    }

    public final boolean dispatch(Callback receiver) {
        return dispatch(receiver, null, null);
    }

    public final boolean dispatch(Callback receiver, DispatcherState state, Object target) {
        switch (mAction) {
            case ACTION_DOWN: {
                mFlags &= ~FLAG_TRACKING;
                boolean res = receiver.onKeyDown(mKeyCode, this);
                if (state != null) {
                    if (res && mRepeatCount == 0 && (mFlags & FLAG_TRACKING) != 0) {
                        state.startTracking(this, target);
                    } else if (isLongPress() && state.isTracking(this)) {
                        if (receiver.onKeyLongPress(mKeyCode, this)) {
                            state.reset();
                            res = true;
                        }
                    }
                }
                return res;
            }
            case ACTION_UP:
                if (state != null) state.handleUpEvent(this);
                return receiver.onKeyUp(mKeyCode, this);
            case ACTION_MULTIPLE: {
                final int count = mRepeatCount;
                final int code = mKeyCode;
                if (receiver.onKeyMultiple(code, count, this)) return true;
                if (code != KEYCODE_UNKNOWN) {
                    mAction = ACTION_DOWN;
                    mRepeatCount = 0;
                    boolean handled = receiver.onKeyDown(code, this);
                    if (handled) {
                        mAction = ACTION_UP;
                        receiver.onKeyUp(code, this);
                    }
                    mAction = ACTION_MULTIPLE;
                    mRepeatCount = count;
                    return handled;
                }
                return false;
            }
        }
        return false;
    }

    public static String actionToString(int action) {
        switch (action) {
            case ACTION_DOWN: return "ACTION_DOWN";
            case ACTION_UP: return "ACTION_UP";
            case ACTION_MULTIPLE: return "ACTION_MULTIPLE";
            default: return Integer.toString(action);
        }
    }

    public static String keyCodeToString(int keyCode) {
        return "KEYCODE_" + keyCode;
    }

    public static int keyCodeFromString(String symbolicName) {
        if (symbolicName.startsWith("KEYCODE_")) symbolicName = symbolicName.substring(8);
        try {
            return Integer.parseInt(symbolicName);
        } catch (NumberFormatException e) {
            return KEYCODE_UNKNOWN;
        }
    }

    @Override
    public String toString() {
        return "KeyEvent { action=" + actionToString(mAction) + ", keyCode=" + mKeyCode + ", unicode=" + mUnicode
                + ", metaState=0x" + Integer.toHexString(mMetaState) + ", repeatCount=" + mRepeatCount
                + ", eventTime=" + mEventTime + " }";
    }
}
