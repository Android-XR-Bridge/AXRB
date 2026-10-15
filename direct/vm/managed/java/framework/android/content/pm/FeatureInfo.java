package android.content.pm;

public class FeatureInfo {
    public static final int GL_ES_VERSION_UNDEFINED = 0;
    public static final int FLAG_REQUIRED = 0x0001;

    public String name;
    public int version;
    public int reqGlEsVersion;
    public int flags;

    public FeatureInfo() {}

    public FeatureInfo(FeatureInfo orig) {
        name = orig.name;
        version = orig.version;
        reqGlEsVersion = orig.reqGlEsVersion;
        flags = orig.flags;
    }

    public String getGlEsVersion() {
        int major = ((reqGlEsVersion & 0xffff0000) >> 16);
        int minor = reqGlEsVersion & 0x0000ffff;
        return major + "." + minor;
    }

    public String toString() {
        return name != null ? "FeatureInfo{" + name + " v=" + version + "}" : "FeatureInfo{glEsVers=" + getGlEsVersion() + "}";
    }
}
