package android.app;

import android.content.ContentResolver;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.util.HashMap;

/** The application's base context: its package, private storage and resources. */
final class ContextImpl extends Context {
    private static final String TAG = "ContextImpl";
    private static final HashMap<String, SharedPreferencesImpl> sSharedPrefs = new HashMap<String, SharedPreferencesImpl>();

    private final ActivityThread mThread;
    private final ApplicationInfo mInfo;
    private final ClassLoader mClassLoader;
    private final AssetManager mAssets;
    private final Resources mResources;
    private int mThemeResource;

    ContextImpl(ActivityThread thread, ApplicationInfo info, ClassLoader loader, AssetManager assets,
            Resources resources) {
        mThread = thread;
        mInfo = info;
        mClassLoader = loader;
        mAssets = assets;
        mResources = resources;
    }

    @Override
    public AssetManager getAssets() {
        return mAssets;
    }

    @Override
    public Resources getResources() {
        return mResources;
    }

    @Override
    public Looper getMainLooper() {
        return Looper.getMainLooper();
    }

    @Override
    public Context getApplicationContext() {
        return mThread.getApplication();
    }

    @Override
    public String getPackageName() {
        return mInfo.packageName;
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return mInfo;
    }

    @Override
    public String getPackageCodePath() {
        return mInfo.sourceDir;
    }

    @Override
    public String getPackageResourcePath() {
        return mInfo.sourceDir;
    }

    @Override
    public ClassLoader getClassLoader() {
        return mClassLoader;
    }

    // --- storage --------------------------------------------------------------

    private void checkMode(int mode) {
        if (mInfo.targetSdkVersion >= 24) {
            if ((mode & MODE_WORLD_READABLE) != 0) throw new SecurityException("MODE_WORLD_READABLE no longer supported");
            if ((mode & MODE_WORLD_WRITEABLE) != 0) {
                throw new SecurityException("MODE_WORLD_WRITEABLE no longer supported");
            }
        }
    }

    private static File ensurePrivateDirExists(File file) {
        if (!file.exists() && !file.mkdirs() && !file.exists()) {
            Log.w(TAG, "Unable to create files subdir " + file.getPath());
        }
        return file;
    }

    private static File makeFilename(File base, String name) {
        if (name.indexOf(File.separatorChar) < 0) return new File(base, name);
        throw new IllegalArgumentException("File " + name + " contains a path separator");
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        if (name == null) {
            if (mInfo.targetSdkVersion < 19) {
                name = "null";
            } else {
                throw new NullPointerException("name == null");
            }
        }
        checkMode(mode);
        File file = getSharedPreferencesPath(name);
        synchronized (ContextImpl.class) {
            SharedPreferencesImpl sp = sSharedPrefs.get(file.getPath());
            if (sp == null) {
                sp = new SharedPreferencesImpl(file);
                sSharedPrefs.put(file.getPath(), sp);
            }
            return sp;
        }
    }

    private File getPreferencesDir() {
        return new File(getDataDir(), "shared_prefs");
    }

    File getSharedPreferencesPath(String name) {
        return makeFilename(getPreferencesDir(), name + ".xml");
    }

    @Override
    public boolean deleteSharedPreferences(String name) {
        synchronized (ContextImpl.class) {
            File prefs = getSharedPreferencesPath(name);
            File backup = SharedPreferencesImpl.makeBackupFile(prefs);
            sSharedPrefs.remove(prefs.getPath());
            prefs.delete();
            backup.delete();
            return !prefs.exists() && !backup.exists();
        }
    }

    @Override
    public FileInputStream openFileInput(String name) throws FileNotFoundException {
        return new FileInputStream(makeFilename(getFilesDir(), name));
    }

    @Override
    public FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException {
        checkMode(mode);
        final boolean append = (mode & MODE_APPEND) != 0;
        return new FileOutputStream(makeFilename(getFilesDir(), name), append);
    }

    @Override
    public boolean deleteFile(String name) {
        return makeFilename(getFilesDir(), name).delete();
    }

    @Override
    public File getFileStreamPath(String name) {
        return makeFilename(getFilesDir(), name);
    }

    @Override
    public String[] fileList() {
        String[] list = getFilesDir().list();
        return list != null ? list : new String[0];
    }

    @Override
    public File getDataDir() {
        return new File(mInfo.dataDir);
    }

    @Override
    public File getFilesDir() {
        return ensurePrivateDirExists(new File(getDataDir(), "files"));
    }

    @Override
    public File getNoBackupFilesDir() {
        return ensurePrivateDirExists(new File(getDataDir(), "no_backup"));
    }

    @Override
    public File getCacheDir() {
        return ensurePrivateDirExists(new File(getDataDir(), "cache"));
    }

    @Override
    public File getCodeCacheDir() {
        return ensurePrivateDirExists(new File(getDataDir(), "code_cache"));
    }

    @Override
    public File getDatabasePath(String name) {
        if (name.charAt(0) == File.separatorChar) return new File(name);
        return makeFilename(new File(getDataDir(), "databases"), name);
    }

    @Override
    public File getDir(String name, int mode) {
        checkMode(mode);
        return ensurePrivateDirExists(makeFilename(getDataDir(), "app_" + name));
    }

    // --- services and components ------------------------------------------------

    private final HashMap<String, Object> mServices = new HashMap<String, Object>();
    private PackageManager mPackageManager;

    @Override
    public Object getSystemService(String name) {
        synchronized (mServices) {
            Object s = mServices.get(name);
            if (s != null) return s;
            s = createService(name);
            if (s == null) {
                Log.w(TAG, "system service \"" + name + "\" is not available in this runner");
                return null;
            }
            mServices.put(name, s);
            return s;
        }
    }

    private Object createService(String name) {
        if (LAYOUT_INFLATER_SERVICE.equals(name)) return new com.android.internal.policy.PhoneLayoutInflater(this);
        if (WINDOW_SERVICE.equals(name)) return new android.view.WindowManagerImpl(this);
        if (ACTIVITY_SERVICE.equals(name)) return new ActivityManager(this);
        if (INPUT_METHOD_SERVICE.equals(name)) return android.view.inputmethod.InputMethodManager.getInstance();
        if (AUDIO_SERVICE.equals(name)) return new android.media.AudioManager(this);
        if (CONNECTIVITY_SERVICE.equals(name)) return new android.net.ConnectivityManager(this);
        if (CLIPBOARD_SERVICE.equals(name)) return new android.content.ClipboardManager(this);
        if (DISPLAY_SERVICE.equals(name)) return new android.hardware.display.DisplayManager(this);
        if (SENSOR_SERVICE.equals(name)) return new android.hardware.SensorManager();
        if (TELEPHONY_SERVICE.equals(name)) return new android.telephony.TelephonyManager(this);
        if (CAMERA_SERVICE.equals(name)) return new android.hardware.camera2.CameraManager(this);
        if (POWER_SERVICE.equals(name)) return new android.os.PowerManager();
        if (LOCATION_SERVICE.equals(name)) return new android.location.LocationManager();
        if (VIBRATOR_SERVICE.equals(name)) return new android.os.Vibrator();
        if (BATTERY_SERVICE.equals(name)) return new android.os.BatteryManager();
        if (WIFI_SERVICE.equals(name)) return new android.net.wifi.WifiManager(this);
        if (INPUT_SERVICE.equals(name)) return new android.hardware.input.InputManager();
        return null;
    }

    @Override
    public PackageManager getPackageManager() {
        if (mPackageManager == null) {
            try {
                mPackageManager = new ApplicationPackageManager(AppManifest.parse(mResources, mInfo), mResources);
            } catch (Exception e) {
                throw new RuntimeException("cannot read the package manifest", e);
            }
        }
        return mPackageManager;
    }

    @Override
    public Intent registerReceiver(android.content.BroadcastReceiver receiver, android.content.IntentFilter filter,
            int flags) {
        // Sticky broadcasts the system keeps: the battery state.
        if (filter != null && filter.hasAction(Intent.ACTION_BATTERY_CHANGED)) {
            Intent sticky = new Intent(Intent.ACTION_BATTERY_CHANGED);
            sticky.putExtra("level", 100);
            sticky.putExtra("scale", 100);
            sticky.putExtra("status", 5);
            sticky.putExtra("plugged", 1);
            sticky.putExtra("present", true);
            sticky.putExtra("temperature", 300);
            return sticky;
        }
        return null;
    }

    @Override
    public void unregisterReceiver(android.content.BroadcastReceiver receiver) {}

    @Override
    public boolean bindService(Intent service, android.content.ServiceConnection conn, int flags) {
        Log.w(TAG, "bindService(" + service + "): no such service in this runner");
        return false;
    }

    @Override
    public File getExternalFilesDir(String type) {
        File base = new File("/storage/emulated/0/Android/data/" + mInfo.packageName + "/files");
        File dir = type != null ? new File(base, type) : base;
        return ensurePrivateDirExists(dir);
    }

    @Override
    public File getExternalCacheDir() {
        return ensurePrivateDirExists(new File("/storage/emulated/0/Android/data/" + mInfo.packageName + "/cache"));
    }

    @Override
    public File getObbDir() {
        return new File("/storage/emulated/0/Android/obb/" + mInfo.packageName);
    }

    @Override
    public void startActivity(Intent intent) {
        mThread.startActivity(null, intent, -1);
    }

    @Override
    public int checkPermission(String permission, int pid, int uid) {
        if (permission == null) throw new IllegalArgumentException("permission is null");
        // Like an emulator install with -g: every permission is granted.
        return PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public int checkSelfPermission(String permission) {
        return checkPermission(permission, android.os.Process.myPid(), android.os.Process.myUid());
    }

    @Override
    public void setTheme(int resid) {
        mThemeResource = resid;
        mTheme = null;
    }

    private Resources.Theme mTheme;
    private ContentResolver mContentResolver;

    @Override
    public Resources.Theme getTheme() {
        if (mTheme == null) {
            mTheme = mResources.newTheme();
            if (mThemeResource != 0) mTheme.applyStyle(mThemeResource, true);
        }
        return mTheme;
    }

    @Override
    public ContentResolver getContentResolver() {
        if (mContentResolver == null) mContentResolver = new ContentResolver(this) {};
        return mContentResolver;
    }
}
