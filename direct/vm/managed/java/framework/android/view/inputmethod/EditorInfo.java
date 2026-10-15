package android.view.inputmethod;

import android.os.Bundle;
import android.text.InputType;

/** Describes a text editor to an input method. */
public class EditorInfo implements InputType {
    public static final int IME_MASK_ACTION = 0x000000ff;
    public static final int IME_ACTION_UNSPECIFIED = 0x00000000;
    public static final int IME_ACTION_NONE = 0x00000001;
    public static final int IME_ACTION_GO = 0x00000002;
    public static final int IME_ACTION_SEARCH = 0x00000003;
    public static final int IME_ACTION_SEND = 0x00000004;
    public static final int IME_ACTION_NEXT = 0x00000005;
    public static final int IME_ACTION_DONE = 0x00000006;
    public static final int IME_ACTION_PREVIOUS = 0x00000007;
    public static final int IME_FLAG_NO_PERSONALIZED_LEARNING = 0x1000000;
    public static final int IME_FLAG_NO_FULLSCREEN = 0x2000000;
    public static final int IME_FLAG_NAVIGATE_PREVIOUS = 0x4000000;
    public static final int IME_FLAG_NAVIGATE_NEXT = 0x8000000;
    public static final int IME_FLAG_NO_EXTRACT_UI = 0x10000000;
    public static final int IME_FLAG_NO_ACCESSORY_ACTION = 0x20000000;
    public static final int IME_FLAG_NO_ENTER_ACTION = 0x40000000;
    public static final int IME_FLAG_FORCE_ASCII = 0x80000000;
    public static final int IME_NULL = 0x00000000;

    public int inputType = TYPE_NULL;
    public int imeOptions = IME_NULL;
    public String privateImeOptions;
    public CharSequence actionLabel;
    public int actionId;
    public int initialSelStart = -1;
    public int initialSelEnd = -1;
    public int initialCapsMode;
    public CharSequence hintText;
    public CharSequence label;
    public String packageName;
    public int fieldId;
    public String fieldName;
    public Bundle extras;
    public String[] contentMimeTypes;

    public final void makeCompatible(int targetSdkVersion) {}

    @Override
    public String toString() {
        return "EditorInfo{inputType=0x" + Integer.toHexString(inputType) + " imeOptions=0x"
                + Integer.toHexString(imeOptions) + " fieldId=" + fieldId + "}";
    }
}
