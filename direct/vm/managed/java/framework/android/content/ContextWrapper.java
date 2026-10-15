package android.content;

import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Looper;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;

public class ContextWrapper extends Context {
    Context mBase;

    public ContextWrapper(Context base) {
        mBase = base;
    }

    protected void attachBaseContext(Context base) {
        if (mBase != null) throw new IllegalStateException("Base context already set");
        mBase = base;
    }

    public Context getBaseContext() {
        return mBase;
    }

    @Override
    public AssetManager getAssets() {
        return mBase.getAssets();
    }

    @Override
    public Resources getResources() {
        return mBase.getResources();
    }

    @Override
    public Looper getMainLooper() {
        return mBase.getMainLooper();
    }

    @Override
    public Context getApplicationContext() {
        return mBase.getApplicationContext();
    }

    @Override
    public String getPackageName() {
        return mBase.getPackageName();
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return mBase.getApplicationInfo();
    }

    @Override
    public String getPackageCodePath() {
        return mBase.getPackageCodePath();
    }

    @Override
    public String getPackageResourcePath() {
        return mBase.getPackageResourcePath();
    }

    @Override
    public ClassLoader getClassLoader() {
        return mBase.getClassLoader();
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        return mBase.getSharedPreferences(name, mode);
    }

    @Override
    public boolean deleteSharedPreferences(String name) {
        return mBase.deleteSharedPreferences(name);
    }

    @Override
    public FileInputStream openFileInput(String name) throws FileNotFoundException {
        return mBase.openFileInput(name);
    }

    @Override
    public FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException {
        return mBase.openFileOutput(name, mode);
    }

    @Override
    public boolean deleteFile(String name) {
        return mBase.deleteFile(name);
    }

    @Override
    public File getFileStreamPath(String name) {
        return mBase.getFileStreamPath(name);
    }

    @Override
    public String[] fileList() {
        return mBase.fileList();
    }

    @Override
    public File getDataDir() {
        return mBase.getDataDir();
    }

    @Override
    public File getFilesDir() {
        return mBase.getFilesDir();
    }

    @Override
    public File getNoBackupFilesDir() {
        return mBase.getNoBackupFilesDir();
    }

    @Override
    public File getCacheDir() {
        return mBase.getCacheDir();
    }

    @Override
    public File getCodeCacheDir() {
        return mBase.getCodeCacheDir();
    }

    @Override
    public File getDatabasePath(String name) {
        return mBase.getDatabasePath(name);
    }

    @Override
    public File getDir(String name, int mode) {
        return mBase.getDir(name, mode);
    }

    @Override
    public Object getSystemService(String name) {
        return mBase.getSystemService(name);
    }

    @Override
    public android.content.pm.PackageManager getPackageManager() {
        return mBase.getPackageManager();
    }

    @Override
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter, int flags) {
        return mBase.registerReceiver(receiver, filter, flags);
    }

    @Override
    public void unregisterReceiver(BroadcastReceiver receiver) {
        mBase.unregisterReceiver(receiver);
    }

    @Override
    public void sendBroadcast(Intent intent) {
        mBase.sendBroadcast(intent);
    }

    @Override
    public boolean bindService(Intent service, ServiceConnection conn, int flags) {
        return mBase.bindService(service, conn, flags);
    }

    @Override
    public void unbindService(ServiceConnection conn) {
        mBase.unbindService(conn);
    }

    @Override
    public ComponentName startService(Intent service) {
        return mBase.startService(service);
    }

    @Override
    public java.io.File getExternalFilesDir(String type) {
        return mBase.getExternalFilesDir(type);
    }

    @Override
    public java.io.File getExternalCacheDir() {
        return mBase.getExternalCacheDir();
    }

    @Override
    public java.io.File getObbDir() {
        return mBase.getObbDir();
    }

    @Override
    public void startActivity(Intent intent) {
        mBase.startActivity(intent);
    }

    @Override
    public int checkPermission(String permission, int pid, int uid) {
        return mBase.checkPermission(permission, pid, uid);
    }

    @Override
    public int checkSelfPermission(String permission) {
        return mBase.checkSelfPermission(permission);
    }

    @Override
    public void setTheme(int resid) {
        mBase.setTheme(resid);
    }

    @Override
    public android.content.res.Resources.Theme getTheme() {
        return mBase.getTheme();
    }

    @Override
    public ContentResolver getContentResolver() {
        return mBase.getContentResolver();
    }
}
