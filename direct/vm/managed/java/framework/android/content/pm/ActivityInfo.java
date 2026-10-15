package android.content.pm;

public class ActivityInfo extends ComponentInfo {
    public static final int LAUNCH_MULTIPLE = 0;
    public static final int LAUNCH_SINGLE_TOP = 1;
    public static final int LAUNCH_SINGLE_TASK = 2;
    public static final int LAUNCH_SINGLE_INSTANCE = 3;
    public static final int LAUNCH_SINGLE_INSTANCE_PER_TASK = 4;

    public static final int DOCUMENT_LAUNCH_NONE = 0;

    public static final int SCREEN_ORIENTATION_UNSET = -2;
    public static final int SCREEN_ORIENTATION_UNSPECIFIED = -1;
    public static final int SCREEN_ORIENTATION_LANDSCAPE = 0;
    public static final int SCREEN_ORIENTATION_PORTRAIT = 1;
    public static final int SCREEN_ORIENTATION_USER = 2;
    public static final int SCREEN_ORIENTATION_BEHIND = 3;
    public static final int SCREEN_ORIENTATION_SENSOR = 4;
    public static final int SCREEN_ORIENTATION_NOSENSOR = 5;
    public static final int SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6;
    public static final int SCREEN_ORIENTATION_SENSOR_PORTRAIT = 7;
    public static final int SCREEN_ORIENTATION_REVERSE_LANDSCAPE = 8;
    public static final int SCREEN_ORIENTATION_REVERSE_PORTRAIT = 9;
    public static final int SCREEN_ORIENTATION_FULL_SENSOR = 10;
    public static final int SCREEN_ORIENTATION_USER_LANDSCAPE = 11;
    public static final int SCREEN_ORIENTATION_USER_PORTRAIT = 12;
    public static final int SCREEN_ORIENTATION_FULL_USER = 13;
    public static final int SCREEN_ORIENTATION_LOCKED = 14;

    public static final int FLAG_MULTIPROCESS = 0x0001;
    public static final int FLAG_FINISH_ON_TASK_LAUNCH = 0x0002;
    public static final int FLAG_CLEAR_TASK_ON_LAUNCH = 0x0004;
    public static final int FLAG_ALWAYS_RETAIN_TASK_STATE = 0x0008;
    public static final int FLAG_STATE_NOT_NEEDED = 0x0010;
    public static final int FLAG_EXCLUDE_FROM_RECENTS = 0x0020;
    public static final int FLAG_ALLOW_TASK_REPARENTING = 0x0040;
    public static final int FLAG_NO_HISTORY = 0x0080;
    public static final int FLAG_FINISH_ON_CLOSE_SYSTEM_DIALOGS = 0x0100;
    public static final int FLAG_HARDWARE_ACCELERATED = 0x0200;
    public static final int FLAG_IMMERSIVE = 0x0800;
    public static final int FLAG_RELINQUISH_TASK_IDENTITY = 0x1000;
    public static final int FLAG_AUTO_REMOVE_FROM_RECENTS = 0x2000;
    public static final int FLAG_RESUME_WHILE_PAUSING = 0x4000;
    public static final int FLAG_ENABLE_VR_MODE = 0x8000;

    public static final int CONFIG_MCC = 0x0001;
    public static final int CONFIG_MNC = 0x0002;
    public static final int CONFIG_LOCALE = 0x0004;
    public static final int CONFIG_TOUCHSCREEN = 0x0008;
    public static final int CONFIG_KEYBOARD = 0x0010;
    public static final int CONFIG_KEYBOARD_HIDDEN = 0x0020;
    public static final int CONFIG_NAVIGATION = 0x0040;
    public static final int CONFIG_ORIENTATION = 0x0080;
    public static final int CONFIG_SCREEN_LAYOUT = 0x0100;
    public static final int CONFIG_UI_MODE = 0x0200;
    public static final int CONFIG_SCREEN_SIZE = 0x0400;
    public static final int CONFIG_SMALLEST_SCREEN_SIZE = 0x0800;
    public static final int CONFIG_DENSITY = 0x1000;
    public static final int CONFIG_LAYOUT_DIRECTION = 0x2000;
    public static final int CONFIG_COLOR_MODE = 0x4000;
    public static final int CONFIG_FONT_SCALE = 0x40000000;

    public static final int RESIZE_MODE_UNRESIZEABLE = 0;
    public static final int RESIZE_MODE_RESIZEABLE = 2;

    public int theme;
    public int launchMode;
    public int documentLaunchMode;
    public String permission;
    public String taskAffinity;
    public String targetActivity;
    public int flags;
    public int screenOrientation = SCREEN_ORIENTATION_UNSPECIFIED;
    public int configChanges;
    public int softInputMode;
    public int uiOptions = 0;
    public String parentActivityName;
    public int persistableMode;
    public int maxRecents;
    public int colorMode;
    public int resizeMode = RESIZE_MODE_RESIZEABLE;
    public int rotationAnimation;
    public WindowLayout windowLayout;

    public ActivityInfo() {}

    public ActivityInfo(ActivityInfo orig) {
        super(orig);
        theme = orig.theme;
        launchMode = orig.launchMode;
        documentLaunchMode = orig.documentLaunchMode;
        permission = orig.permission;
        taskAffinity = orig.taskAffinity;
        targetActivity = orig.targetActivity;
        flags = orig.flags;
        screenOrientation = orig.screenOrientation;
        configChanges = orig.configChanges;
        softInputMode = orig.softInputMode;
        uiOptions = orig.uiOptions;
        parentActivityName = orig.parentActivityName;
        persistableMode = orig.persistableMode;
        maxRecents = orig.maxRecents;
        colorMode = orig.colorMode;
        resizeMode = orig.resizeMode;
        rotationAnimation = orig.rotationAnimation;
        windowLayout = orig.windowLayout;
    }

    public final int getThemeResource() {
        return theme != 0 ? theme : (applicationInfo != null ? applicationInfo.theme : 0);
    }

    public static final class WindowLayout {
        public final int width;
        public final float widthFraction;
        public final int height;
        public final float heightFraction;
        public final int gravity;
        public final int minWidth;
        public final int minHeight;

        public WindowLayout(int width, float widthFraction, int height, float heightFraction, int gravity,
                int minWidth, int minHeight) {
            this.width = width;
            this.widthFraction = widthFraction;
            this.height = height;
            this.heightFraction = heightFraction;
            this.gravity = gravity;
            this.minWidth = minWidth;
            this.minHeight = minHeight;
        }
    }

    @Override
    public String toString() {
        return "ActivityInfo{" + Integer.toHexString(System.identityHashCode(this)) + " " + name + "}";
    }
}
