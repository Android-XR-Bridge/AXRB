package com.axrb.runtime;

import android.os.Looper;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.Intent;
import android.content.ComponentName;
import android.content.res.Configuration;
import android.os.Binder;
import android.os.Process;
import java.io.File;

/** Real AOSP framework entry point for Direct-mode applications. */
public final class AppProcess {
    private static native boolean stopRequested();
    private static HostWindow window;
    private static Object activityThread;
    private static PackageInfo application;
    private static final java.util.ArrayList<Object> activityStack = new java.util.ArrayList<>();
    private static final java.util.Map<android.os.IBinder, Object[]> activityResults = new java.util.HashMap<>();
    private static final java.util.Map<Object, HostWindow> activityWindows = new java.util.HashMap<>();

    static boolean finishActivity(android.os.IBinder token, int resultCode, Intent result) {
        new android.os.Handler(Looper.getMainLooper()).post(() -> {
            try {
                Object finished = null;
                for (Object record : activityStack) if (Reflect.field(record, "token") == token) finished = record;
                if (finished == null) return;
                Object activity = Reflect.field(finished, "activity");
                if ((Integer) Reflect.call(finished, "getLifecycleState") == 3) Reflect.call(activity, "performPause");
                Reflect.call(activity, "performStop", false, "AXRB finish");
                Reflect.call(activity, "performDestroy");
                activityStack.remove(finished);
                Object[] returnTo = activityResults.remove(token);
                if (returnTo != null && returnTo[0] != null) for (Object record : activityStack)
                    if (Reflect.field(record, "token") == returnTo[0])
                        Reflect.call(Reflect.field(record, "activity"), "dispatchActivityResult", returnTo[1], returnTo[2], resultCode, result, "AXRB activity result");
                if (!activityStack.isEmpty()) {
                    Object previous = activityStack.get(activityStack.size() - 1);
                    Reflect.call(activityThread, "performResumeActivity", previous, true, "AXRB return");
                    window = activityWindows.get(previous);
                    window.attach((android.app.Activity) Reflect.field(previous, "activity"));
                }
            } catch (Exception error) { throw new RuntimeException("Activity finish failed", error); }
        });
        return true;
    }

    static int requestPermissions(Intent intent, android.os.IBinder caller, String who,
            int requestCode, HostPackageManager packages) {
        String[] requested = intent.getStringArrayExtra("android.content.pm.extra.REQUEST_PERMISSIONS_NAMES");
        if (requested == null || caller == null || requestCode < 0) return -92;
        String[] names = requested.clone();
        int[] results = new int[names.length];
        for (int i = 0; i < names.length; i++)
            results[i] = names[i] == null ? PackageManager.PERMISSION_DENIED : packages.permission(names[i]);
        Intent result = new Intent()
                .putExtra("android.content.pm.extra.REQUEST_PERMISSIONS_NAMES", names)
                .putExtra("android.content.pm.extra.REQUEST_PERMISSIONS_RESULTS", results);
        // Preserve AOSP Activity/Fragment callback routing and asynchronous
        // completion. Unsupported permissions stay denied by the same policy
        // used by checkSelfPermission; no camera/microphone access is granted.
        new android.os.Handler(Looper.getMainLooper()).post(() -> {
            try {
                for (Object record : activityStack) if (Reflect.field(record, "token") == caller) {
                    Reflect.call(Reflect.field(record, "activity"), "dispatchActivityResult", who,
                            requestCode, android.app.Activity.RESULT_OK, result, "AXRB permission result");
                    System.out.println("AXRB_PERMISSION_RESULT " + java.util.Arrays.toString(names)
                            + " " + java.util.Arrays.toString(results));
                    break;
                }
            } catch (Exception error) { throw new RuntimeException("Permission result delivery failed", error); }
        });
        return 0;
    }

    static int startActivity(Intent intent, android.os.IBinder caller, String who, int requestCode) {
        ComponentName component = intent.getComponent();
        if (component == null || application == null || !application.packageName.equals(component.getPackageName())) return -92;
        ActivityInfo target = null;
        for (ActivityInfo candidate : application.activities) if (candidate.name.equals(component.getClassName())) target = candidate;
        if (target == null) return -92;
        ActivityInfo selected = target;
        new android.os.Handler(Looper.getMainLooper()).post(() -> {
            try {
                if (!activityStack.isEmpty()) {
                    Object previous = activityStack.get(activityStack.size() - 1);
                    Reflect.call(Reflect.field(previous, "activity"), "performPause");
                    Reflect.call(previous, "setState", 4);
                }
                launchActivity(activityThread, application, selected, intent, caller, who, requestCode);
            } catch (Exception error) { throw new RuntimeException("Activity launch failed", error); }
        });
        return 0;
    }
    public static void main(String[] args) {
        try {
            System.out.println("AXRB_AOSP_BOOTSTRAP");
            System.load("/system/lib64/libaxrb_art_bridge.so");
            Reflect.call(Class.forName("android.graphics.Typeface"), "loadPreinstalledSystemFontMap");
            if (java.util.Arrays.asList(args).contains("--diagnostics")) {
                System.setProperty("axrb.inspectViews", "true");
                Thread watchdog = new Thread(() -> {
                    for (int snapshot = 0; snapshot < 5; ++snapshot) {
                    try { Thread.sleep(15000); } catch (InterruptedException ignored) { return; }
                    for (java.util.Map.Entry<Thread, StackTraceElement[]> entry : Thread.getAllStackTraces().entrySet()) {
                        System.err.println("AXRB_THREAD " + entry.getKey().getName());
                        for (StackTraceElement frame : entry.getValue()) System.err.println("  at " + frame);
                    }
                    }
                }, "AXRB startup diagnostics");
                watchdog.setDaemon(true);
                watchdog.start();
            }
            Reflect.set(Class.forName("android.view.ThreadedRenderer"), "sRendererEnabled", false);
            HostServices services = new HostServices();
            services.install();
            Looper.prepareMainLooper();
            Object thread = Reflect.call(Class.forName("android.app.ActivityThread"), "systemMain");
            Object context = Reflect.call(thread, "getSystemContext");
            services.attach((Context) context);
            String legacyDriver = (String) Reflect.call(Class.forName("android.os.SystemProperties"), "get", "ro.axrb.legacy_oculus_driver");
            if (!legacyDriver.isEmpty()) {
                services.packages.offerLegacyDriver((Context) context, legacyDriver);
                System.out.println("AXRB_AOSP_LEGACY_DRIVER " + legacyDriver);
            }
            Class<?> properties = Class.forName("android.os.SystemProperties");
            String countText = (String) Reflect.call(properties, "get", "ro.axrb.companion_count");
            int companionCount = countText.isEmpty() ? 0 : Integer.parseInt(countText);
            if (companionCount < 0 || companionCount > 8) throw new IllegalArgumentException("Invalid companion package count");
            for (int i = 0; i < companionCount; ++i) {
                String path = (String) Reflect.call(properties, "get", "ro.axrb.companion_apk." + i);
                services.packages.offerCompanion((Context) context, path);
                System.out.println("AXRB_AOSP_COMPANION " + path);
            }
            Object configuration = Reflect.field(thread, "mConfigurationController");
            Configuration initial = new Configuration(((Context) context).getResources().getConfiguration());
            initial.densityDpi = 320;
            initial.screenWidthDp = 960;
            initial.screenHeightDp = 540;
            initial.smallestScreenWidthDp = 540;
            initial.orientation = Configuration.ORIENTATION_LANDSCAPE;
            Reflect.call(configuration, "setConfiguration", initial);
            Reflect.call(configuration, "setCompatConfiguration", initial);
            System.out.println("AXRB_AOSP_CONTEXT " + context);
            if (args.length > 0) {
                PackageInfo info = ((Context) context).getPackageManager().getPackageArchiveInfo(args[0],
                        PackageManager.GET_ACTIVITIES | PackageManager.GET_SERVICES |
                        PackageManager.GET_PROVIDERS | PackageManager.GET_META_DATA | PackageManager.GET_PERMISSIONS);
                if (info == null) throw new IllegalArgumentException("Cannot parse APK " + args[0]);
                services.packages.installGame(info);
                if (args.length > 1) services.packages.launcherActivity = args[1];
                System.out.println("AXRB_AOSP_PACKAGE " + info.packageName);
                if (args.length > 1) launch(thread, info, args);
            }
        } catch (Throwable error) {
            error.printStackTrace();
            System.exit(1);
        }
    }

    private static void launch(Object thread, PackageInfo info, String[] args) throws Exception {
        ApplicationInfo app = info.applicationInfo;
        app.sourceDir = app.publicSourceDir = args[0];
        app.dataDir = "/data/data/" + info.packageName;
        app.deviceProtectedDataDir = app.dataDir;
        Reflect.set(app, "credentialProtectedDataDir", app.dataDir);
        app.nativeLibraryDir = new File(args[0]).getParent() + "/lib/arm64";
        app.uid = Process.myUid();
        Reflect.set(app, "primaryCpuAbi", "arm64-v8a");
        for (String dir : new String[]{"files", "cache", "code_cache", "shared_prefs", "databases"})
            new File(app.dataDir, dir).mkdirs();
        ActivityInfo activity = null;
        for (ActivityInfo candidate : info.activities) if (candidate.name.equals(args[1])) activity = candidate;
        if (activity == null) throw new IllegalArgumentException("Activity not in manifest: " + args[1]);
        activity.applicationInfo = app;
        Object loaded = Reflect.call(thread, "getPackageInfoNoCheck", app);
        activityThread = thread;
        application = info;
        Object bound = Reflect.create("android.app.ActivityThread$AppBindData");
        Reflect.set(bound, "appInfo", app);
        Reflect.set(bound, "info", loaded);
        Reflect.set(bound, "processName", app.processName == null ? app.packageName : app.processName);
        Reflect.set(thread, "mBoundApplication", bound);
        Object appInstance = Reflect.call(loaded, "makeApplicationInner", false, Reflect.field(thread, "mInstrumentation"));
        Reflect.set(thread, "mInitialApplication", appInstance);
        launchActivity(thread, info, activity, new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
                .setComponent(new ComponentName(info.packageName, activity.name)), null, null, -1);
        android.os.Handler handler = new android.os.Handler(Looper.getMainLooper());
        handler.postDelayed(new Runnable() {
            public void run() {
                if (!stopRequested()) { handler.postDelayed(this, 250); return; }
                try {
                    for (int i = activityStack.size() - 1; i >= 0; --i) {
                        Object record = activityStack.get(i);
                        Object current = Reflect.field(record, "activity");
                        int state = (Integer)Reflect.call(record, "getLifecycleState");
                        if (state == 3) Reflect.call(current, "performPause");
                        if (state < 5) Reflect.call(current, "performStop", false, "AXRB shutdown");
                    }
                    // ActivityThread normally drains QueuedWork after stopping
                    // activities. In this host-driven lifecycle we must do it
                    // explicitly: SharedPreferences.apply() can still be queued
                    // when onStop returns, and System.exit would lose that save.
                    Reflect.call(Class.forName("android.app.QueuedWork"), "waitToFinish");
                    // Some engines intentionally terminate their process in
                    // onDestroy. Record completion of the save lifecycle first.
                    System.out.println("AXRB_AOSP_ACTIVITIES_STOPPED");
                    for (int i = activityStack.size() - 1; i >= 0; --i) {
                        Object current = Reflect.field(activityStack.get(i), "activity");
                        Reflect.call(current, "performDestroy");
                    }
                    // Also finish asynchronous writes requested in onDestroy.
                    Reflect.call(Class.forName("android.app.QueuedWork"), "waitToFinish");
                    System.out.println("AXRB_AOSP_SESSION_CLOSED");
                    System.exit(0);
                } catch (Throwable error) {
                    error.printStackTrace();
                    System.exit(1);
                }
            }
        }, 250);
        Looper.loop();
    }

    private static void launchActivity(Object thread, PackageInfo info, ActivityInfo activity, Intent intent,
                                       android.os.IBinder caller, String who, int requestCode) throws Exception {
        activity.applicationInfo = info.applicationInfo;
        Object loaded = Reflect.call(thread, "getPackageInfoNoCheck", info.applicationInfo);
        Object record = Reflect.create("android.app.ActivityThread$ActivityClientRecord");
        Binder token = new Binder();
        activityResults.put(token, new Object[]{caller, who, requestCode});
        HostActivities.orientations.put(token, activity.screenOrientation);
        Reflect.set(record, "token", token);
        Reflect.set(record, "assistToken", new Binder());
        Reflect.set(record, "shareableActivityToken", new Binder());
        Reflect.set(record, "activityInfo", activity);
        Reflect.set(record, "packageInfo", loaded);
        Reflect.set(record, "intent", intent);
        activityStack.add(record);
        Reflect.set(record, "overrideConfig", new Configuration());
        System.out.println("AXRB_AOSP_LAUNCH " + activity.name);
        Object launched = Reflect.call(thread, "performLaunchActivity", record, null);
        if (launched == null) throw new IllegalStateException("Activity creation returned null");
        System.out.println("AXRB_AOSP_ACTIVITY_CREATED " + activity.name);
        window = new HostWindow();
        HostWindow createdWindow = window;
        activityWindows.put(record, createdWindow);
        new android.os.Handler(Looper.getMainLooper()).postDelayed(() -> {
            try {
                Reflect.call(thread, "handleStartActivity", record, null, null);
                Object resumed = Reflect.call(thread, "performResumeActivity", record, true, "AXRB launch");
                System.out.println("AXRB_AOSP_LIFECYCLE resumed=" + resumed + " state=" + Reflect.call(record, "getLifecycleState")
                        + " finished=" + ((android.app.Activity) launched).isFinishing());
                createdWindow.attach((android.app.Activity) launched);
            } catch (Exception error) {
                throw new RuntimeException("Host window attachment failed", error);
            }
        }, 100);
    }
}
