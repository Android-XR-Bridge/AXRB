package android.view;

import android.graphics.PixelFormat;

public interface WindowManager extends ViewManager {
    /** Thrown when a window cannot be added (for example, a bad token). */
    public static class BadTokenException extends RuntimeException {
        public BadTokenException() {}

        public BadTokenException(String name) {
            super(name);
        }
    }

    public static class InvalidDisplayException extends RuntimeException {
        public InvalidDisplayException() {}

        public InvalidDisplayException(String name) {
            super(name);
        }
    }

    Display getDefaultDisplay();

    void removeViewImmediate(View view);

    public static class LayoutParams extends ViewGroup.LayoutParams {
        public int x;
        public int y;
        public float horizontalWeight;
        public float verticalWeight;
        public int type;

        public static final int FIRST_APPLICATION_WINDOW = 1;
        public static final int TYPE_BASE_APPLICATION = 1;
        public static final int TYPE_APPLICATION = 2;
        public static final int TYPE_APPLICATION_STARTING = 3;
        public static final int TYPE_DRAWN_APPLICATION = 4;
        public static final int LAST_APPLICATION_WINDOW = 99;
        public static final int FIRST_SUB_WINDOW = 1000;
        public static final int TYPE_APPLICATION_PANEL = FIRST_SUB_WINDOW;
        public static final int TYPE_APPLICATION_MEDIA = FIRST_SUB_WINDOW + 1;
        public static final int TYPE_APPLICATION_SUB_PANEL = FIRST_SUB_WINDOW + 2;
        public static final int TYPE_APPLICATION_ATTACHED_DIALOG = FIRST_SUB_WINDOW + 3;
        public static final int LAST_SUB_WINDOW = 1999;
        public static final int FIRST_SYSTEM_WINDOW = 2000;
        public static final int TYPE_STATUS_BAR = FIRST_SYSTEM_WINDOW;
        public static final int TYPE_SEARCH_BAR = FIRST_SYSTEM_WINDOW + 1;
        public static final int TYPE_PHONE = FIRST_SYSTEM_WINDOW + 2;
        public static final int TYPE_SYSTEM_ALERT = FIRST_SYSTEM_WINDOW + 3;
        public static final int TYPE_TOAST = FIRST_SYSTEM_WINDOW + 5;
        public static final int TYPE_SYSTEM_OVERLAY = FIRST_SYSTEM_WINDOW + 6;
        public static final int TYPE_SYSTEM_DIALOG = FIRST_SYSTEM_WINDOW + 8;
        public static final int TYPE_SYSTEM_ERROR = FIRST_SYSTEM_WINDOW + 10;
        public static final int TYPE_INPUT_METHOD = FIRST_SYSTEM_WINDOW + 11;
        public static final int TYPE_APPLICATION_OVERLAY = FIRST_SYSTEM_WINDOW + 38;
        public static final int LAST_SYSTEM_WINDOW = 2999;

        public static final int FLAG_ALLOW_LOCK_WHILE_SCREEN_ON = 0x00000001;
        public static final int FLAG_DIM_BEHIND = 0x00000002;
        public static final int FLAG_BLUR_BEHIND = 0x00000004;
        public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
        public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
        public static final int FLAG_NOT_TOUCH_MODAL = 0x00000020;
        public static final int FLAG_TOUCHABLE_WHEN_WAKING = 0x00000040;
        public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
        public static final int FLAG_LAYOUT_IN_SCREEN = 0x00000100;
        public static final int FLAG_LAYOUT_NO_LIMITS = 0x00000200;
        public static final int FLAG_FULLSCREEN = 0x00000400;
        public static final int FLAG_FORCE_NOT_FULLSCREEN = 0x00000800;
        public static final int FLAG_DITHER = 0x00001000;
        public static final int FLAG_SECURE = 0x00002000;
        public static final int FLAG_SCALED = 0x00004000;
        public static final int FLAG_IGNORE_CHEEK_PRESSES = 0x00008000;
        public static final int FLAG_LAYOUT_INSET_DECOR = 0x00010000;
        public static final int FLAG_ALT_FOCUSABLE_IM = 0x00020000;
        public static final int FLAG_WATCH_OUTSIDE_TOUCH = 0x00040000;
        public static final int FLAG_SHOW_WHEN_LOCKED = 0x00080000;
        public static final int FLAG_SHOW_WALLPAPER = 0x00100000;
        public static final int FLAG_TURN_SCREEN_ON = 0x00200000;
        public static final int FLAG_DISMISS_KEYGUARD = 0x00400000;
        public static final int FLAG_SPLIT_TOUCH = 0x00800000;
        public static final int FLAG_HARDWARE_ACCELERATED = 0x01000000;
        public static final int FLAG_LAYOUT_IN_OVERSCAN = 0x02000000;
        public static final int FLAG_TRANSLUCENT_STATUS = 0x04000000;
        public static final int FLAG_TRANSLUCENT_NAVIGATION = 0x08000000;
        public static final int FLAG_LOCAL_FOCUS_MODE = 0x10000000;
        public static final int FLAG_LAYOUT_ATTACHED_IN_DECOR = 0x40000000;
        public static final int FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS = 0x80000000;

        public static final int SOFT_INPUT_MASK_STATE = 0x0f;
        public static final int SOFT_INPUT_STATE_UNSPECIFIED = 0;
        public static final int SOFT_INPUT_STATE_UNCHANGED = 1;
        public static final int SOFT_INPUT_STATE_HIDDEN = 2;
        public static final int SOFT_INPUT_STATE_ALWAYS_HIDDEN = 3;
        public static final int SOFT_INPUT_STATE_VISIBLE = 4;
        public static final int SOFT_INPUT_STATE_ALWAYS_VISIBLE = 5;
        public static final int SOFT_INPUT_MASK_ADJUST = 0xf0;
        public static final int SOFT_INPUT_ADJUST_UNSPECIFIED = 0x00;
        public static final int SOFT_INPUT_ADJUST_RESIZE = 0x10;
        public static final int SOFT_INPUT_ADJUST_PAN = 0x20;
        public static final int SOFT_INPUT_ADJUST_NOTHING = 0x30;
        public static final int SOFT_INPUT_IS_FORWARD_NAVIGATION = 0x100;

        public static final float BRIGHTNESS_OVERRIDE_NONE = -1.0f;
        public static final float BRIGHTNESS_OVERRIDE_OFF = 0.0f;
        public static final float BRIGHTNESS_OVERRIDE_FULL = 1.0f;

        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT = 0;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES = 1;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER = 2;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS = 3;

        public static final int ROTATION_ANIMATION_ROTATE = 0;

        public static final int FIRST_CHANGED = 1 << 0;
        public static final int LAYOUT_CHANGED = 1 << 0;
        public static final int TYPE_CHANGED = 1 << 1;
        public static final int FLAGS_CHANGED = 1 << 2;
        public static final int FORMAT_CHANGED = 1 << 3;
        public static final int ANIMATION_CHANGED = 1 << 4;
        public static final int DIM_AMOUNT_CHANGED = 1 << 5;
        public static final int TITLE_CHANGED = 1 << 6;
        public static final int ALPHA_CHANGED = 1 << 7;
        public static final int SOFT_INPUT_MODE_CHANGED = 1 << 9;

        public int flags;
        public int privateFlags;
        public int softInputMode;
        public int gravity;
        public float horizontalMargin;
        public float verticalMargin;
        public int format;
        public int windowAnimations;
        public float alpha = 1.0f;
        public float dimAmount = 1.0f;
        public int preferredDisplayModeId;
        public float screenBrightness = BRIGHTNESS_OVERRIDE_NONE;
        public float buttonBrightness = BRIGHTNESS_OVERRIDE_NONE;
        public int rotationAnimation = ROTATION_ANIMATION_ROTATE;
        public Object token;
        public String packageName;
        public int screenOrientation = -1;
        public float preferredRefreshRate;
        public int systemUiVisibility;
        public int layoutInDisplayCutoutMode;
        private CharSequence mTitle = "";

        public LayoutParams() {
            super(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
            type = TYPE_APPLICATION;
            format = PixelFormat.OPAQUE;
        }

        public LayoutParams(int _type) {
            super(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
            type = _type;
            format = PixelFormat.OPAQUE;
        }

        public LayoutParams(int _type, int _flags) {
            this(_type);
            flags = _flags;
        }

        public LayoutParams(int _type, int _flags, int _format) {
            this(_type, _flags);
            format = _format;
        }

        public LayoutParams(int w, int h, int _type, int _flags, int _format) {
            super(w, h);
            type = _type;
            flags = _flags;
            format = _format;
        }

        public LayoutParams(int w, int h, int xpos, int ypos, int _type, int _flags, int _format) {
            super(w, h);
            x = xpos;
            y = ypos;
            type = _type;
            flags = _flags;
            format = _format;
        }

        public final void setTitle(CharSequence title) {
            mTitle = title == null ? "" : title;
        }

        public final CharSequence getTitle() {
            return mTitle != null ? mTitle : "";
        }

        public static boolean mayUseInputMethod(int flags) {
            return (flags & (FLAG_NOT_FOCUSABLE | FLAG_ALT_FOCUSABLE_IM)) != FLAG_NOT_FOCUSABLE;
        }

        public void setFitInsetsTypes(int types) {}

        public void setFitInsetsSides(int sides) {}

        public final int copyFrom(LayoutParams o) {
            int changes = 0;
            if (width != o.width) {
                width = o.width;
                changes |= LAYOUT_CHANGED;
            }
            if (height != o.height) {
                height = o.height;
                changes |= LAYOUT_CHANGED;
            }
            if (x != o.x) {
                x = o.x;
                changes |= LAYOUT_CHANGED;
            }
            if (y != o.y) {
                y = o.y;
                changes |= LAYOUT_CHANGED;
            }
            if (horizontalWeight != o.horizontalWeight) {
                horizontalWeight = o.horizontalWeight;
                changes |= LAYOUT_CHANGED;
            }
            if (verticalWeight != o.verticalWeight) {
                verticalWeight = o.verticalWeight;
                changes |= LAYOUT_CHANGED;
            }
            if (horizontalMargin != o.horizontalMargin) {
                horizontalMargin = o.horizontalMargin;
                changes |= LAYOUT_CHANGED;
            }
            if (verticalMargin != o.verticalMargin) {
                verticalMargin = o.verticalMargin;
                changes |= LAYOUT_CHANGED;
            }
            if (type != o.type) {
                type = o.type;
                changes |= TYPE_CHANGED;
            }
            if (flags != o.flags) {
                flags = o.flags;
                changes |= FLAGS_CHANGED;
            }
            if (privateFlags != o.privateFlags) {
                privateFlags = o.privateFlags;
                changes |= FLAGS_CHANGED;
            }
            if (softInputMode != o.softInputMode) {
                softInputMode = o.softInputMode;
                changes |= SOFT_INPUT_MODE_CHANGED;
            }
            if (gravity != o.gravity) {
                gravity = o.gravity;
                changes |= LAYOUT_CHANGED;
            }
            if (format != o.format) {
                format = o.format;
                changes |= FORMAT_CHANGED;
            }
            if (windowAnimations != o.windowAnimations) {
                windowAnimations = o.windowAnimations;
                changes |= ANIMATION_CHANGED;
            }
            if (token == null) token = o.token;
            if (packageName == null) packageName = o.packageName;
            if (!getTitle().toString().equals(o.getTitle().toString())) {
                mTitle = o.mTitle;
                changes |= TITLE_CHANGED;
            }
            if (alpha != o.alpha) {
                alpha = o.alpha;
                changes |= ALPHA_CHANGED;
            }
            if (dimAmount != o.dimAmount) {
                dimAmount = o.dimAmount;
                changes |= DIM_AMOUNT_CHANGED;
            }
            screenBrightness = o.screenBrightness;
            buttonBrightness = o.buttonBrightness;
            screenOrientation = o.screenOrientation;
            systemUiVisibility = o.systemUiVisibility;
            layoutInDisplayCutoutMode = o.layoutInDisplayCutoutMode;
            return changes;
        }

        @Override
        public String toString() {
            return "WM.LayoutParams{(" + x + "," + y + ")(" + sizeToString(width) + "x" + sizeToString(height)
                    + ") gr=0x" + Integer.toHexString(gravity) + " ty=" + type + " fl=0x"
                    + Integer.toHexString(flags) + " title=" + getTitle() + "}";
        }
    }
}
