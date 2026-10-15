package android.provider;

import android.content.ContentResolver;
import android.net.Uri;

public final class Settings {
    public static final String ACTION_SETTINGS = "android.settings.SETTINGS";
    public static final String ACTION_APPLICATION_DETAILS_SETTINGS = "android.settings.APPLICATION_DETAILS_SETTINGS";

    private Settings() {}

    public static class SettingNotFoundException extends android.util.AndroidException {
        public SettingNotFoundException(String msg) {
            super(msg);
        }
    }

    public static class NameValueTable {
        public static final String NAME = "name";
        public static final String VALUE = "value";
    }

    public static final class System extends NameValueTable {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/system");
        public static final String SCREEN_BRIGHTNESS = "screen_brightness";
        public static final String ACCELEROMETER_ROTATION = "accelerometer_rotation";
        public static final String FONT_SCALE = "font_scale";
        public static final String TIME_12_24 = "time_12_24";

        public static String getString(ContentResolver resolver, String name) {
            if (ACCELEROMETER_ROTATION.equals(name)) return "0";
            if (SCREEN_BRIGHTNESS.equals(name)) return "200";
            if (FONT_SCALE.equals(name)) return "1.0";
            if (TIME_12_24.equals(name)) return "24";
            return null;
        }

        public static int getInt(ContentResolver cr, String name, int def) {
            String v = getString(cr, name);
            try {
                return v != null ? Integer.parseInt(v) : def;
            } catch (NumberFormatException e) {
                return def;
            }
        }

        public static int getInt(ContentResolver cr, String name) throws SettingNotFoundException {
            String v = getString(cr, name);
            if (v == null) throw new SettingNotFoundException(name);
            return Integer.parseInt(v);
        }

        public static float getFloat(ContentResolver cr, String name, float def) {
            String v = getString(cr, name);
            return v != null ? Float.parseFloat(v) : def;
        }

        public static boolean putInt(ContentResolver cr, String name, int value) {
            return false;
        }

        public static Uri getUriFor(String name) {
            return Uri.withAppendedPath(CONTENT_URI, name);
        }
    }

    public static final class Secure extends NameValueTable {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/secure");
        public static final String ANDROID_ID = "android_id";

        public static String getString(ContentResolver resolver, String name) {
            if (ANDROID_ID.equals(name)) return "a1b2c3d4e5f60718";
            return null;
        }

        public static int getInt(ContentResolver cr, String name, int def) {
            return def;
        }

        public static Uri getUriFor(String name) {
            return Uri.withAppendedPath(CONTENT_URI, name);
        }
    }

    public static final class Global extends NameValueTable {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/global");
        public static final String AIRPLANE_MODE_ON = "airplane_mode_on";
        public static final String DEVICE_NAME = "device_name";

        public static String getString(ContentResolver resolver, String name) {
            if (AIRPLANE_MODE_ON.equals(name)) return "0";
            if (DEVICE_NAME.equals(name)) return "AXRB";
            return null;
        }

        public static int getInt(ContentResolver cr, String name, int def) {
            String v = getString(cr, name);
            return v != null ? Integer.parseInt(v) : def;
        }

        public static float getFloat(ContentResolver cr, String name, float def) {
            return def;
        }

        public static Uri getUriFor(String name) {
            return Uri.withAppendedPath(CONTENT_URI, name);
        }
    }
}
