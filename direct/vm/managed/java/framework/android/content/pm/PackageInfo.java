package android.content.pm;

public class PackageInfo {
    public static final int INSTALL_LOCATION_AUTO = 0;
    public static final int INSTALL_LOCATION_INTERNAL_ONLY = 1;
    public static final int INSTALL_LOCATION_PREFER_EXTERNAL = 2;
    public static final int REQUESTED_PERMISSION_GRANTED = 1 << 1;

    public String packageName;
    public String[] splitNames;
    @Deprecated
    public int versionCode;
    public int versionCodeMajor;
    public String versionName;
    public int baseRevisionCode;
    public String sharedUserId;
    public int sharedUserLabel;
    public ApplicationInfo applicationInfo;
    public long firstInstallTime;
    public long lastUpdateTime;
    public int[] gids;
    public ActivityInfo[] activities;
    public ActivityInfo[] receivers;
    public PermissionInfo[] permissions;
    public String[] requestedPermissions;
    public int[] requestedPermissionsFlags;
    @Deprecated
    public Signature[] signatures;
    public int installLocation = INSTALL_LOCATION_INTERNAL_ONLY;

    public PackageInfo() {}

    public long getLongVersionCode() {
        return (((long) versionCodeMajor) << 32) | (versionCode & 0xffffffffL);
    }

    public void setLongVersionCode(long longVersionCode) {
        versionCodeMajor = (int) (longVersionCode >> 32);
        versionCode = (int) longVersionCode;
    }

    @Override
    public String toString() {
        return "PackageInfo{" + Integer.toHexString(System.identityHashCode(this)) + " " + packageName + "}";
    }
}
