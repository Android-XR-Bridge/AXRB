package android.content.pm;

public class PermissionInfo extends PackageItemInfo {
    public static final int PROTECTION_NORMAL = 0;
    public static final int PROTECTION_DANGEROUS = 1;
    public static final int PROTECTION_SIGNATURE = 2;

    public int protectionLevel;
    public String group;
    public int descriptionRes;

    public PermissionInfo() {}

    public int getProtection() {
        return protectionLevel & 0xf;
    }
}
