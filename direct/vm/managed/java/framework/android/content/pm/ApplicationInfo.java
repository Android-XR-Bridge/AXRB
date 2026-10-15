package android.content.pm;

public class ApplicationInfo extends PackageItemInfo {
    public static final int FLAG_SYSTEM = 1 << 0;
    public static final int FLAG_DEBUGGABLE = 1 << 1;
    public static final int FLAG_HAS_CODE = 1 << 2;
    public static final int FLAG_PERSISTENT = 1 << 3;
    public static final int FLAG_FACTORY_TEST = 1 << 4;
    public static final int FLAG_ALLOW_TASK_REPARENTING = 1 << 5;
    public static final int FLAG_ALLOW_CLEAR_USER_DATA = 1 << 6;
    public static final int FLAG_UPDATED_SYSTEM_APP = 1 << 7;
    public static final int FLAG_TEST_ONLY = 1 << 8;
    public static final int FLAG_SUPPORTS_SMALL_SCREENS = 1 << 9;
    public static final int FLAG_SUPPORTS_NORMAL_SCREENS = 1 << 10;
    public static final int FLAG_SUPPORTS_LARGE_SCREENS = 1 << 11;
    public static final int FLAG_RESIZEABLE_FOR_SCREENS = 1 << 12;
    public static final int FLAG_SUPPORTS_SCREEN_DENSITIES = 1 << 13;
    public static final int FLAG_VM_SAFE_MODE = 1 << 14;
    public static final int FLAG_ALLOW_BACKUP = 1 << 15;
    public static final int FLAG_KILL_AFTER_RESTORE = 1 << 16;
    public static final int FLAG_RESTORE_ANY_VERSION = 1 << 17;
    public static final int FLAG_EXTERNAL_STORAGE = 1 << 18;
    public static final int FLAG_SUPPORTS_XLARGE_SCREENS = 1 << 19;
    public static final int FLAG_LARGE_HEAP = 1 << 20;
    public static final int FLAG_STOPPED = 1 << 21;
    public static final int FLAG_SUPPORTS_RTL = 1 << 22;
    public static final int FLAG_INSTALLED = 1 << 23;
    public static final int FLAG_IS_DATA_ONLY = 1 << 24;
    public static final int FLAG_IS_GAME = 1 << 25;
    public static final int FLAG_FULL_BACKUP_ONLY = 1 << 26;
    public static final int FLAG_USES_CLEARTEXT_TRAFFIC = 1 << 27;
    public static final int FLAG_EXTRACT_NATIVE_LIBS = 1 << 28;
    public static final int FLAG_HARDWARE_ACCELERATED = 1 << 29;
    public static final int FLAG_SUSPENDED = 1 << 30;
    public static final int FLAG_MULTIARCH = 1 << 31;

    public static final int CATEGORY_UNDEFINED = -1;

    public String className;
    public String processName;
    public String taskAffinity;
    public String permission;
    public String sourceDir;
    public String publicSourceDir;
    public String[] splitNames;
    public String[] splitSourceDirs;
    public String[] splitPublicSourceDirs;
    public String dataDir;
    public String deviceProtectedDataDir;
    public String nativeLibraryDir;
    public String[] sharedLibraryFiles;
    public int descriptionRes;
    public int theme;
    public int flags;
    public int uid;
    public int minSdkVersion;
    public int targetSdkVersion;
    public int compatibleWidthLimitDp;
    public int requiresSmallestWidthDp;
    public int largestWidthLimitDp;
    public int category = CATEGORY_UNDEFINED;
    public String manageSpaceActivityName;
    public String backupAgentName;
    public String appComponentFactory;
    public boolean enabled = true;

    public ApplicationInfo() {}

    public ApplicationInfo(ApplicationInfo o) {
        super(o);
        className = o.className;
        processName = o.processName;
        taskAffinity = o.taskAffinity;
        permission = o.permission;
        sourceDir = o.sourceDir;
        publicSourceDir = o.publicSourceDir;
        splitNames = o.splitNames;
        splitSourceDirs = o.splitSourceDirs;
        splitPublicSourceDirs = o.splitPublicSourceDirs;
        dataDir = o.dataDir;
        deviceProtectedDataDir = o.deviceProtectedDataDir;
        nativeLibraryDir = o.nativeLibraryDir;
        sharedLibraryFiles = o.sharedLibraryFiles;
        descriptionRes = o.descriptionRes;
        theme = o.theme;
        flags = o.flags;
        uid = o.uid;
        minSdkVersion = o.minSdkVersion;
        targetSdkVersion = o.targetSdkVersion;
        category = o.category;
        manageSpaceActivityName = o.manageSpaceActivityName;
        backupAgentName = o.backupAgentName;
        appComponentFactory = o.appComponentFactory;
        enabled = o.enabled;
    }

    @Override
    ApplicationInfo getApplicationInfo() {
        return this;
    }

    public CharSequence loadDescription(PackageManager pm) {
        if (descriptionRes != 0) return pm.getText(packageName, descriptionRes, this);
        return null;
    }

    public boolean isResourceOverlay() {
        return false;
    }

    public boolean isProfileable() {
        return false;
    }

    @Override
    public String toString() {
        return "ApplicationInfo{" + Integer.toHexString(System.identityHashCode(this)) + " " + packageName + "}";
    }
}
