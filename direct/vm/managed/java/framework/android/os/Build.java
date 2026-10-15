package android.os;

/**
 * Build information. These values identify this runtime honestly; they do not
 * imitate a certified device (spec 2.2, 11).
 */
public class Build {
    public static final String UNKNOWN = "unknown";
    public static final String MANUFACTURER = "androitainer";
    public static final String BRAND = "androitainer";
    public static final String MODEL = "Windows desktop runner";
    public static final String DEVICE = "desktop";
    public static final String PRODUCT = "desktop_x86_64";
    public static final String BOARD = "desktop";
    public static final String HARDWARE = "windows";
    public static final String DISPLAY = "androitainer desktop-native-v0";
    public static final String ID = "ARUN0";
    public static final String TAGS = "dev-keys";
    public static final String TYPE = "userdebug";
    public static final String FINGERPRINT = "androitainer/desktop_x86_64/desktop:10/ARUN0/0:userdebug/dev-keys";
    public static final String BOOTLOADER = UNKNOWN;
    public static final String HOST = UNKNOWN;
    public static final String USER = UNKNOWN;
    public static final long TIME = 0;
    public static final String[] SUPPORTED_ABIS = {"x86_64"};
    public static final String[] SUPPORTED_64_BIT_ABIS = {"x86_64"};
    public static final String[] SUPPORTED_32_BIT_ABIS = {};
    @Deprecated
    public static final String CPU_ABI = "x86_64";
    @Deprecated
    public static final String CPU_ABI2 = "";
    @Deprecated
    public static final String SERIAL = UNKNOWN;

    public static String getSerial() {
        throw new SecurityException("getSerial: no device serial is exposed by this runtime");
    }

    public static class VERSION {
        public static final int SDK_INT = 29;
        public static final String RELEASE = "10";
        public static final String CODENAME = "REL";
        public static final String INCREMENTAL = "0";
        public static final int PREVIEW_SDK_INT = 0;
        public static final String SECURITY_PATCH = "";
        public static final String BASE_OS = "";
        @Deprecated
        public static final String SDK = "29";
    }

    public static class VERSION_CODES {
        public static final int CUR_DEVELOPMENT = 10000;
        public static final int BASE = 1;
        public static final int LOLLIPOP = 21;
        public static final int LOLLIPOP_MR1 = 22;
        public static final int M = 23;
        public static final int N = 24;
        public static final int N_MR1 = 25;
        public static final int O = 26;
        public static final int O_MR1 = 27;
        public static final int P = 28;
        public static final int Q = 29;
        public static final int R = 30;
    }
}
