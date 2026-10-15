package android.content.res;

import android.os.LocaleList;
import java.util.Locale;

/** The device configuration resources are selected for. */
public final class Configuration implements Comparable<Configuration> {
    public static final int ORIENTATION_UNDEFINED = 0;
    public static final int ORIENTATION_PORTRAIT = 1;
    public static final int ORIENTATION_LANDSCAPE = 2;

    public static final int UI_MODE_TYPE_MASK = 0x0f;
    public static final int UI_MODE_TYPE_UNDEFINED = 0x00;
    public static final int UI_MODE_TYPE_NORMAL = 0x01;
    public static final int UI_MODE_TYPE_DESK = 0x02;
    public static final int UI_MODE_TYPE_CAR = 0x03;
    public static final int UI_MODE_TYPE_TELEVISION = 0x04;
    public static final int UI_MODE_TYPE_APPLIANCE = 0x05;
    public static final int UI_MODE_TYPE_WATCH = 0x06;
    public static final int UI_MODE_TYPE_VR_HEADSET = 0x07;
    public static final int UI_MODE_NIGHT_MASK = 0x30;
    public static final int UI_MODE_NIGHT_UNDEFINED = 0x00;
    public static final int UI_MODE_NIGHT_NO = 0x10;
    public static final int UI_MODE_NIGHT_YES = 0x20;

    public static final int SCREENLAYOUT_SIZE_MASK = 0x0f;
    public static final int SCREENLAYOUT_SIZE_UNDEFINED = 0x00;
    public static final int SCREENLAYOUT_SIZE_SMALL = 0x01;
    public static final int SCREENLAYOUT_SIZE_NORMAL = 0x02;
    public static final int SCREENLAYOUT_SIZE_LARGE = 0x03;
    public static final int SCREENLAYOUT_SIZE_XLARGE = 0x04;
    public static final int SCREENLAYOUT_LONG_MASK = 0x30;
    public static final int SCREENLAYOUT_LONG_UNDEFINED = 0x00;
    public static final int SCREENLAYOUT_LONG_NO = 0x10;
    public static final int SCREENLAYOUT_LONG_YES = 0x20;
    public static final int SCREENLAYOUT_LAYOUTDIR_MASK = 0xC0;
    public static final int SCREENLAYOUT_LAYOUTDIR_SHIFT = 6;
    public static final int SCREENLAYOUT_LAYOUTDIR_UNDEFINED = 0x00;
    public static final int SCREENLAYOUT_LAYOUTDIR_LTR = 0x01 << SCREENLAYOUT_LAYOUTDIR_SHIFT;
    public static final int SCREENLAYOUT_LAYOUTDIR_RTL = 0x02 << SCREENLAYOUT_LAYOUTDIR_SHIFT;
    public static final int SCREENLAYOUT_ROUND_MASK = 0x300;
    public static final int SCREENLAYOUT_ROUND_NO = 0x1 << 8;
    public static final int SCREENLAYOUT_ROUND_YES = 0x2 << 8;
    public static final int SCREENLAYOUT_UNDEFINED = 0;

    public static final int TOUCHSCREEN_UNDEFINED = 0;
    public static final int TOUCHSCREEN_NOTOUCH = 1;
    public static final int TOUCHSCREEN_FINGER = 3;
    public static final int KEYBOARD_UNDEFINED = 0;
    public static final int KEYBOARD_NOKEYS = 1;
    public static final int KEYBOARD_QWERTY = 2;
    public static final int KEYBOARD_12KEY = 3;
    public static final int KEYBOARDHIDDEN_UNDEFINED = 0;
    public static final int KEYBOARDHIDDEN_NO = 1;
    public static final int KEYBOARDHIDDEN_YES = 2;
    public static final int HARDKEYBOARDHIDDEN_UNDEFINED = 0;
    public static final int HARDKEYBOARDHIDDEN_NO = 1;
    public static final int HARDKEYBOARDHIDDEN_YES = 2;
    public static final int NAVIGATION_UNDEFINED = 0;
    public static final int NAVIGATION_NONAV = 1;
    public static final int NAVIGATION_DPAD = 2;
    public static final int NAVIGATION_TRACKBALL = 3;
    public static final int NAVIGATION_WHEEL = 4;
    public static final int NAVIGATIONHIDDEN_UNDEFINED = 0;
    public static final int NAVIGATIONHIDDEN_NO = 1;
    public static final int NAVIGATIONHIDDEN_YES = 2;
    public static final int SCREEN_WIDTH_DP_UNDEFINED = 0;
    public static final int SCREEN_HEIGHT_DP_UNDEFINED = 0;
    public static final int SMALLEST_SCREEN_WIDTH_DP_UNDEFINED = 0;
    public static final int DENSITY_DPI_UNDEFINED = 0;
    public static final int COLOR_MODE_UNDEFINED = 0;

    public float fontScale = 1.0f;
    public int mcc;
    public int mnc;
    @Deprecated
    public Locale locale = Locale.getDefault();
    public int orientation = ORIENTATION_PORTRAIT;
    public int uiMode = UI_MODE_TYPE_NORMAL | UI_MODE_NIGHT_NO;
    public int screenLayout = SCREENLAYOUT_SIZE_NORMAL | SCREENLAYOUT_LONG_NO | SCREENLAYOUT_LAYOUTDIR_LTR;
    public int screenWidthDp;
    public int screenHeightDp;
    public int smallestScreenWidthDp;
    public int densityDpi;
    public int touchscreen = TOUCHSCREEN_FINGER;
    public int keyboard = KEYBOARD_QWERTY;
    public int keyboardHidden = KEYBOARDHIDDEN_NO;
    public int hardKeyboardHidden = HARDKEYBOARDHIDDEN_NO;
    public int navigation = NAVIGATION_NONAV;
    public int navigationHidden = NAVIGATIONHIDDEN_NO;
    public int colorMode;

    public Configuration() {}

    public Configuration(Configuration o) {
        setTo(o);
    }

    public void setTo(Configuration o) {
        fontScale = o.fontScale;
        mcc = o.mcc;
        mnc = o.mnc;
        locale = o.locale;
        orientation = o.orientation;
        uiMode = o.uiMode;
        screenLayout = o.screenLayout;
        screenWidthDp = o.screenWidthDp;
        screenHeightDp = o.screenHeightDp;
        smallestScreenWidthDp = o.smallestScreenWidthDp;
        densityDpi = o.densityDpi;
        touchscreen = o.touchscreen;
        keyboard = o.keyboard;
        keyboardHidden = o.keyboardHidden;
        hardKeyboardHidden = o.hardKeyboardHidden;
        navigation = o.navigation;
        navigationHidden = o.navigationHidden;
        colorMode = o.colorMode;
    }

    public void setToDefaults() {
        setTo(new Configuration());
    }

    public LocaleList getLocales() {
        return new LocaleList(locale != null ? locale : Locale.getDefault());
    }

    public void setLocales(LocaleList locales) {
        locale = locales == null || locales.isEmpty() ? Locale.getDefault() : locales.get(0);
    }

    public void setLocale(Locale loc) {
        locale = loc;
    }

    public int getLayoutDirection() {
        return (screenLayout & SCREENLAYOUT_LAYOUTDIR_MASK) == SCREENLAYOUT_LAYOUTDIR_RTL ? 1 : 0;
    }

    public void setLayoutDirection(Locale loc) {
        screenLayout = (screenLayout & ~SCREENLAYOUT_LAYOUTDIR_MASK) | SCREENLAYOUT_LAYOUTDIR_LTR;
    }

    public boolean isLayoutSizeAtLeast(int size) {
        int cur = screenLayout & SCREENLAYOUT_SIZE_MASK;
        if (cur == SCREENLAYOUT_SIZE_UNDEFINED) return false;
        return cur >= size;
    }

    public boolean isScreenRound() {
        return (screenLayout & SCREENLAYOUT_ROUND_MASK) == SCREENLAYOUT_ROUND_YES;
    }

    public boolean isNightModeActive() {
        return (uiMode & UI_MODE_NIGHT_MASK) == UI_MODE_NIGHT_YES;
    }

    public int diff(Configuration delta) {
        int changed = 0;
        if (fontScale != delta.fontScale) changed |= 0x40000000;
        if (locale == null ? delta.locale != null : !locale.equals(delta.locale)) changed |= 0x0004;
        if (orientation != delta.orientation) changed |= 0x0080;
        if (screenLayout != delta.screenLayout) changed |= 0x0100;
        if (uiMode != delta.uiMode) changed |= 0x0200;
        if (screenWidthDp != delta.screenWidthDp || screenHeightDp != delta.screenHeightDp) changed |= 0x0400;
        if (smallestScreenWidthDp != delta.smallestScreenWidthDp) changed |= 0x0800;
        if (densityDpi != delta.densityDpi) changed |= 0x1000;
        return changed;
    }

    @Override
    public int compareTo(Configuration o) {
        if (fontScale != o.fontScale) return fontScale < o.fontScale ? -1 : 1;
        if (orientation != o.orientation) return orientation - o.orientation;
        if (uiMode != o.uiMode) return uiMode - o.uiMode;
        if (screenLayout != o.screenLayout) return screenLayout - o.screenLayout;
        if (screenWidthDp != o.screenWidthDp) return screenWidthDp - o.screenWidthDp;
        if (screenHeightDp != o.screenHeightDp) return screenHeightDp - o.screenHeightDp;
        if (smallestScreenWidthDp != o.smallestScreenWidthDp) return smallestScreenWidthDp - o.smallestScreenWidthDp;
        if (locale != null && o.locale != null && !locale.equals(o.locale)) {
            return locale.toString().compareTo(o.locale.toString());
        }
        return densityDpi - o.densityDpi;
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof Configuration && compareTo((Configuration) o) == 0;
    }

    @Override
    public int hashCode() {
        return orientation * 31 + screenWidthDp * 17 + screenHeightDp + densityDpi;
    }

    @Override
    public String toString() {
        return "{" + fontScale + " " + locale + " sw" + smallestScreenWidthDp + "dp w" + screenWidthDp + "dp h"
                + screenHeightDp + "dp " + densityDpi + "dpi " + (orientation == ORIENTATION_LANDSCAPE ? "land" : "port")
                + ((uiMode & UI_MODE_NIGHT_MASK) == UI_MODE_NIGHT_YES ? " night" : "") + "}";
    }
}
