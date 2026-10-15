package com.axrb.storageprobe;
import android.app.Activity;
import android.os.Bundle;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Exercises real AOSP Context storage across independent launcher sessions. */
public final class StorageProbe extends Activity {
    private static final String VALUE = "AXRB save: \u00e4 \u65e5\u672c 12345";
    private static void require(boolean ok, String detail) { if (!ok) throw new AssertionError(detail); }
    private static void write(File file) throws Exception {
        require(file.getParentFile().isDirectory() || file.getParentFile().mkdirs(), "mkdir " + file);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(VALUE.getBytes(StandardCharsets.UTF_8)); out.getFD().sync();
        }
    }
    private static void verify(File file) throws Exception {
        require(VALUE.equals(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8)), "read " + file);
    }
    @Override public void onResume() {
        super.onResume();
        System.out.println("STORAGE_PROBE_READY_FOR_STOP");
    }
    @Override public void onStop() {
        super.onStop();
        getSharedPreferences("lifecycle", MODE_PRIVATE).edit().putString("savedOnStop", VALUE).apply();
        System.out.println("STORAGE_PROBE_ASYNC_SAVE_QUEUED");
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            android.content.pm.PackageManager pm = getPackageManager();
            android.content.pm.InstallSourceInfo source = pm.getInstallSourceInfo(getPackageName());
            require(source.getInstallingPackageName() == null && source.getInitiatingPackageName() == null,
                    "local install has no installer identity");
            require(source.getPackageSource() == android.content.pm.PackageInstaller.PACKAGE_SOURCE_LOCAL_FILE,
                    "local package source");
            try { pm.getInstallSourceInfo("com.axrb.missing"); throw new AssertionError("unknown package accepted"); }
            catch (android.content.pm.PackageManager.NameNotFoundException expected) {}
            require(pm.getInstalledApplications(0).stream().anyMatch(a -> a.packageName.equals(getPackageName())), "installed app list");
            require(pm.getInstalledPackages(0).stream().anyMatch(a -> a.packageName.equals(getPackageName())), "installed package list");
            android.app.ActivityManager am = (android.app.ActivityManager)getSystemService(ACTIVITY_SERVICE);
            require(am.getRunningAppProcesses().stream().anyMatch(p -> p.pid == android.os.Process.myPid()
                    && p.uid == android.os.Process.myUid()), "running process identity");
            android.widget.Toast.makeText(this, "AXRB package probe", android.widget.Toast.LENGTH_SHORT).show();
            System.out.println("PACKAGE_SERVICES_PROBE_PASS");
            String phase = System.getenv("AXRB_STORAGE_TEST_PHASE");
            if ("stop".equals(phase)) return;
            File save = new File(getFilesDir(), "save.json");
            File external = new File(getExternalFilesDir(null), "external-save.txt");
            require(getExternalFilesDir(null) != null, "external storage available");
            SharedPreferences prefs = getSharedPreferences("progress", MODE_PRIVATE);
            SQLiteDatabase db = openOrCreateDatabase("progress.db", MODE_PRIVATE, null);
            db.enableWriteAheadLogging();
            if ("write".equals(phase)) {
                File temporary = new File(getFilesDir(), "save.tmp");
                write(temporary); require(temporary.renameTo(save), "atomic rename");
                write(external);
                require(prefs.edit().putString("name", VALUE).putInt("level", 42).commit(), "prefs commit");
                db.execSQL("CREATE TABLE progress (level INTEGER, name TEXT)");
                db.beginTransaction();
                try { db.execSQL("INSERT INTO progress VALUES (?, ?)", new Object[]{42, VALUE}); db.setTransactionSuccessful(); }
                finally { db.endTransaction(); }
                write(new File(getCacheDir(), "cache-test"));
            } else {
                require("read".equals(phase), "phase");
                verify(save); verify(external);
                require(VALUE.equals(getSharedPreferences("lifecycle", MODE_PRIVATE).getString("savedOnStop", "")), "onStop apply survives shutdown");
                require(VALUE.equals(prefs.getString("name", "")) && prefs.getInt("level", 0) == 42, "prefs survive restart");
                try (Cursor c = db.rawQuery("SELECT level, name FROM progress", null)) {
                    require(c.moveToFirst() && c.getInt(0) == 42 && VALUE.equals(c.getString(1)) && !c.moveToNext(), "SQLite survives restart");
                }
                File scratch = new File(getFilesDir(), "delete-test"); write(scratch);
                require(scratch.delete() && !scratch.exists(), "delete");
            }
            db.close();
            System.out.println("STORAGE_PROBE_" + phase.toUpperCase() + "_PASS");
            System.exit(0);
        } catch (Throwable error) { error.printStackTrace(); System.exit(2); }
    }
}
