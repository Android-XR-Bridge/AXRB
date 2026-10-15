package android.os;

import java.io.File;

/** The device's storage layout, as on a single-user phone. */
public class Environment {
    public static final String MEDIA_MOUNTED = "mounted";
    public static final String MEDIA_MOUNTED_READ_ONLY = "mounted_ro";
    public static final String MEDIA_UNMOUNTED = "unmounted";
    public static final String MEDIA_REMOVED = "removed";
    public static final String MEDIA_UNKNOWN = "unknown";

    public static String DIRECTORY_MUSIC = "Music";
    public static String DIRECTORY_PODCASTS = "Podcasts";
    public static String DIRECTORY_RINGTONES = "Ringtones";
    public static String DIRECTORY_ALARMS = "Alarms";
    public static String DIRECTORY_NOTIFICATIONS = "Notifications";
    public static String DIRECTORY_PICTURES = "Pictures";
    public static String DIRECTORY_MOVIES = "Movies";
    public static String DIRECTORY_DOWNLOADS = "Download";
    public static String DIRECTORY_DCIM = "DCIM";
    public static String DIRECTORY_DOCUMENTS = "Documents";
    public static String DIRECTORY_SCREENSHOTS = "Screenshots";

    private static final File EXTERNAL = new File("/storage/emulated/0");

    public static File getRootDirectory() {
        return new File("/system");
    }

    public static File getDataDirectory() {
        return new File("/data");
    }

    public static File getDownloadCacheDirectory() {
        return new File("/data/cache");
    }

    public static File getStorageDirectory() {
        return new File("/storage");
    }

    public static File getExternalStorageDirectory() {
        return EXTERNAL;
    }

    public static File getExternalStoragePublicDirectory(String type) {
        return new File(EXTERNAL, type);
    }

    public static String getExternalStorageState() {
        return MEDIA_MOUNTED;
    }

    public static String getExternalStorageState(File path) {
        return MEDIA_MOUNTED;
    }

    public static String getStorageState(File path) {
        return MEDIA_MOUNTED;
    }

    public static boolean isExternalStorageEmulated() {
        return true;
    }

    public static boolean isExternalStorageEmulated(File path) {
        return true;
    }

    public static boolean isExternalStorageRemovable() {
        return false;
    }

    public static boolean isExternalStorageRemovable(File path) {
        return false;
    }

    public static boolean isExternalStorageManager() {
        return false;
    }

    public static boolean isExternalStorageLegacy() {
        return false;
    }
}
