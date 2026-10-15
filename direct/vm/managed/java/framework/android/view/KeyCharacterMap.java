package android.view;

/** Key character map of the host keyboard (the host delivers the typed character). */
public class KeyCharacterMap {
    public static final int BUILT_IN_KEYBOARD = 0;
    public static final int VIRTUAL_KEYBOARD = -1;
    public static final int NUMERIC = 1;
    public static final int PREDICTIVE = 2;
    public static final int ALPHA = 3;
    public static final int FULL = 4;
    public static final int SPECIAL_FUNCTION = 5;
    public static final char HEX_INPUT = '';
    public static final char PICKER_DIALOG_INPUT = '';
    public static final int COMBINING_ACCENT = 0x80000000;
    public static final int COMBINING_ACCENT_MASK = 0x7FFFFFFF;

    private static final KeyCharacterMap FULL_MAP = new KeyCharacterMap();

    private KeyCharacterMap() {}

    public static KeyCharacterMap load(int deviceId) {
        return FULL_MAP;
    }

    public int get(int keyCode, int metaState) {
        return new KeyEvent(0, 0, KeyEvent.ACTION_DOWN, keyCode, 0, metaState).getUnicodeChar();
    }

    public char getDisplayLabel(int keyCode) {
        return new KeyEvent(KeyEvent.ACTION_DOWN, keyCode).getDisplayLabel();
    }

    public char getNumber(int keyCode) {
        return new KeyEvent(KeyEvent.ACTION_DOWN, keyCode).getNumber();
    }

    public boolean isPrintingKey(int keyCode) {
        return get(keyCode, 0) != 0;
    }

    public int getKeyboardType() {
        return FULL;
    }

    public static boolean deviceHasKey(int keyCode) {
        return keyCode != KeyEvent.KEYCODE_UNKNOWN;
    }

    public KeyEvent[] getEvents(char[] chars) {
        if (chars == null) throw new IllegalArgumentException("chars must not be null.");
        KeyEvent[] events = new KeyEvent[chars.length * 2];
        long now = android.os.SystemClock.uptimeMillis();
        for (int i = 0; i < chars.length; i++) {
            events[i * 2] = KeyEvent.fromHost(KeyEvent.ACTION_DOWN, 0, chars[i], 0, now);
            events[i * 2 + 1] = KeyEvent.fromHost(KeyEvent.ACTION_UP, 0, chars[i], 0, now);
        }
        return events;
    }
}
