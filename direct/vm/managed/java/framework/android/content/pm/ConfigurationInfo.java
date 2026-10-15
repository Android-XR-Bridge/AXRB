package android.content.pm;

public class ConfigurationInfo {
    public static final int GL_ES_VERSION_UNDEFINED = 0;
    public int reqGlEsVersion;
    public int reqTouchScreen;
    public int reqKeyboardType;
    public int reqNavigation;
    public int reqInputFeatures;

    public ConfigurationInfo() {}

    public String getGlEsVersion() {
        int major = (reqGlEsVersion & 0xffff0000) >> 16;
        int minor = reqGlEsVersion & 0x0000ffff;
        return major + "." + minor;
    }
}
