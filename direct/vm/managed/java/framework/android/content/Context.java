package android.content;

import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.os.Looper;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;

public abstract class Context {
    public static final int MODE_PRIVATE = 0x0000;
    public static final int MODE_WORLD_READABLE = 0x0001;
    public static final int MODE_WORLD_WRITEABLE = 0x0002;
    public static final int MODE_APPEND = 0x8000;
    public static final int MODE_MULTI_PROCESS = 0x0004;

    public static final String WINDOW_SERVICE = "window";
    public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";
    public static final String ACTIVITY_SERVICE = "activity";
    public static final String INPUT_METHOD_SERVICE = "input_method";
    public static final String AUDIO_SERVICE = "audio";
    public static final String CONNECTIVITY_SERVICE = "connectivity";
    public static final String VIBRATOR_SERVICE = "vibrator";
    public static final String CLIPBOARD_SERVICE = "clipboard";
    public static final String DISPLAY_SERVICE = "display";
    public static final String SENSOR_SERVICE = "sensor";
    public static final String TELEPHONY_SERVICE = "phone";
    public static final String CAMERA_SERVICE = "camera";
    public static final String POWER_SERVICE = "power";
    public static final String WIFI_SERVICE = "wifi";
    public static final String UI_MODE_SERVICE = "uimode";
    public static final String STORAGE_SERVICE = "storage";
    public static final String KEYGUARD_SERVICE = "keyguard";
    public static final String NOTIFICATION_SERVICE = "notification";
    public static final String ALARM_SERVICE = "alarm";
    public static final String LOCATION_SERVICE = "location";
    public static final String BATTERY_SERVICE = "batterymanager";
    public static final String MEDIA_ROUTER_SERVICE = "media_router";
    public static final String ACCOUNT_SERVICE = "account";
    public static final String ACCESSIBILITY_SERVICE = "accessibility";
    public static final String DOWNLOAD_SERVICE = "download";
    public static final String USB_SERVICE = "usb";
    public static final String NFC_SERVICE = "nfc";
    public static final String WIFI_P2P_SERVICE = "wifip2p";
    public static final String NSD_SERVICE = "servicediscovery";
    public static final String APP_OPS_SERVICE = "appops";
    public static final String USER_SERVICE = "user";
    public static final String CAPTIONING_SERVICE = "captioning";
    public static final String INPUT_SERVICE = "input";
    public static final String MEDIA_SESSION_SERVICE = "media_session";
    public static final String JOB_SCHEDULER_SERVICE = "jobscheduler";
    public static final String USAGE_STATS_SERVICE = "usagestats";
    public static final String TEXT_SERVICES_MANAGER_SERVICE = "textservices";
    public static final String SEARCH_SERVICE = "search";
    public static final String DEVICE_POLICY_SERVICE = "device_policy";
    public static final String DROPBOX_SERVICE = "dropbox";
    public static final String HARDWARE_PROPERTIES_SERVICE = "hardware_properties";
    public static final String SHORTCUT_SERVICE = "shortcut";
    public static final String COMPANION_DEVICE_SERVICE = "companiondevice";
    public static final String VIBRATOR_MANAGER_SERVICE = "vibrator_manager";
    public static final String GAME_SERVICE = "game";
    public static final String DISPLAY_HASH_SERVICE = "display_hash";
    public static final String FINGERPRINT_SERVICE = "fingerprint";
    public static final String BIOMETRIC_SERVICE = "biometric";
    public static final String TELECOM_SERVICE = "telecom";
    public static final String CARRIER_CONFIG_SERVICE = "carrier_config";
    public static final String TELEPHONY_SUBSCRIPTION_SERVICE = "telephony_subscription_service";
    public static final String MIDI_SERVICE = "midi";
    public static final String RESTRICTIONS_SERVICE = "restrictions";
    public static final String APPWIDGET_SERVICE = "appwidget";
    public static final String WALLPAPER_SERVICE = "wallpaper";
    public static final String PRINT_SERVICE = "print";
    public static final String CONSUMER_IR_SERVICE = "consumer_ir";
    public static final String LAUNCHER_APPS_SERVICE = "launcherapps";
    public static final String BLUETOOTH_SERVICE = "bluetooth";

    public static final int BIND_AUTO_CREATE = 0x0001;
    public static final int RECEIVER_EXPORTED = 0x2;
    public static final int RECEIVER_NOT_EXPORTED = 0x4;

    public abstract AssetManager getAssets();

    public abstract Resources getResources();

    public abstract Looper getMainLooper();

    /** Runs tasks on the main thread. */
    public java.util.concurrent.Executor getMainExecutor() {
        final android.os.Handler handler = new android.os.Handler(getMainLooper());
        return new java.util.concurrent.Executor() {
            public void execute(Runnable command) {
                if (!handler.post(command)) throw new java.util.concurrent.RejectedExecutionException(handler + " is shutting down");
            }
        };
    }

    public abstract Context getApplicationContext();

    public abstract String getPackageName();

    public abstract ApplicationInfo getApplicationInfo();

    public abstract String getPackageCodePath();

    public abstract String getPackageResourcePath();

    public abstract ClassLoader getClassLoader();

    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    public abstract boolean deleteSharedPreferences(String name);

    public abstract FileInputStream openFileInput(String name) throws FileNotFoundException;

    public abstract FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException;

    public abstract boolean deleteFile(String name);

    public abstract File getFileStreamPath(String name);

    public abstract String[] fileList();

    public abstract File getDataDir();

    public abstract File getFilesDir();

    public abstract File getNoBackupFilesDir();

    public abstract File getCacheDir();

    public abstract File getCodeCacheDir();

    public abstract File getDatabasePath(String name);

    public abstract File getDir(String name, int mode);

    public abstract Object getSystemService(String name);

    @SuppressWarnings("unchecked")
    public final <T> T getSystemService(Class<T> serviceClass) {
        String name = getSystemServiceName(serviceClass);
        return name != null ? (T) getSystemService(name) : null;
    }

    public String getSystemServiceName(Class<?> serviceClass) {
        String n = serviceClass.getName();
        if (n.equals("android.view.WindowManager")) return WINDOW_SERVICE;
        if (n.equals("android.view.LayoutInflater")) return LAYOUT_INFLATER_SERVICE;
        if (n.equals("android.app.ActivityManager")) return ACTIVITY_SERVICE;
        if (n.equals("android.view.inputmethod.InputMethodManager")) return INPUT_METHOD_SERVICE;
        if (n.equals("android.media.AudioManager")) return AUDIO_SERVICE;
        if (n.equals("android.net.ConnectivityManager")) return CONNECTIVITY_SERVICE;
        if (n.equals("android.content.ClipboardManager")) return CLIPBOARD_SERVICE;
        if (n.equals("android.hardware.display.DisplayManager")) return DISPLAY_SERVICE;
        if (n.equals("android.hardware.SensorManager")) return SENSOR_SERVICE;
        if (n.equals("android.telephony.TelephonyManager")) return TELEPHONY_SERVICE;
        if (n.equals("android.hardware.camera2.CameraManager")) return CAMERA_SERVICE;
        if (n.equals("android.os.PowerManager")) return POWER_SERVICE;
        return null;
    }

    public android.content.pm.PackageManager getPackageManager() {
        return null;
    }

    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        return registerReceiver(receiver, filter, 0);
    }

    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter, int flags) {
        return null;
    }

    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter, String broadcastPermission,
            android.os.Handler scheduler) {
        return registerReceiver(receiver, filter, 0);
    }

    public void unregisterReceiver(BroadcastReceiver receiver) {}

    public void sendBroadcast(Intent intent) {}

    public boolean bindService(Intent service, ServiceConnection conn, int flags) {
        return false;
    }

    public void unbindService(ServiceConnection conn) {}

    public ComponentName startService(Intent service) {
        return null;
    }

    public boolean stopService(Intent service) {
        return false;
    }

    public int checkCallingOrSelfPermission(String permission) {
        return checkSelfPermission(permission);
    }

    public File getExternalFilesDir(String type) {
        return null;
    }

    public File[] getExternalFilesDirs(String type) {
        File f = getExternalFilesDir(type);
        return f != null ? new File[] {f} : new File[0];
    }

    public File getExternalCacheDir() {
        return null;
    }

    public File getObbDir() {
        return null;
    }

    public File[] getObbDirs() {
        File f = getObbDir();
        return f != null ? new File[] {f} : new File[0];
    }

    public Context createConfigurationContext(android.content.res.Configuration overrideConfiguration) {
        return this;
    }

    public Context createPackageContext(String packageName, int flags)
            throws android.content.pm.PackageManager.NameNotFoundException {
        if (packageName.equals(getPackageName())) return this;
        throw new android.content.pm.PackageManager.NameNotFoundException(packageName);
    }

    public abstract void startActivity(Intent intent);

    public abstract int checkPermission(String permission, int pid, int uid);

    public abstract int checkSelfPermission(String permission);

    public abstract void setTheme(int resid);

    public final String getString(int resId) {
        return getResources().getString(resId);
    }

    public final String getString(int resId, Object... formatArgs) {
        return getResources().getString(resId, formatArgs);
    }

    public final CharSequence getText(int resId) {
        return getResources().getText(resId);
    }

    public final int getColor(int id) {
        return getResources().getColor(id, null);
    }

    public boolean isRestricted() {
        return false;
    }

    public Resources.Theme getTheme() {
        return null;
    }

    public ContentResolver getContentResolver() {
        return null;
    }

    public final TypedArray obtainStyledAttributes(int[] attrs) {
        return getTheme().obtainStyledAttributes(attrs);
    }

    public final TypedArray obtainStyledAttributes(int resid, int[] attrs) {
        return getTheme().obtainStyledAttributes(resid, attrs);
    }

    public final TypedArray obtainStyledAttributes(AttributeSet set, int[] attrs) {
        return getTheme().obtainStyledAttributes(set, attrs, 0, 0);
    }

    public final TypedArray obtainStyledAttributes(AttributeSet set, int[] attrs, int defStyleAttr, int defStyleRes) {
        return getTheme().obtainStyledAttributes(set, attrs, defStyleAttr, defStyleRes);
    }

    public final Drawable getDrawable(int id) {
        return getResources().getDrawable(id, getTheme());
    }
}
