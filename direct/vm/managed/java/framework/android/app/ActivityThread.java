package android.app;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import com.android.internal.policy.PhoneWindow;

/**
 * The application's main thread: binds the application, launches the launcher
 * activity and runs the main looper until the session ends (the last activity
 * finished or the host window was closed).
 *
 * Runtime-internal (hidden on Android). The host calls main() on the thread
 * that owns the host window, with the launch description as arguments.
 */
public final class ActivityThread {
    private static final String TAG = "ActivityThread";

    private static ActivityThread sCurrentActivityThread;

    private final Handler mH;
    private ApplicationInfo mInfo;
    private ClassLoader mClassLoader;
    private Application mApplication;
    private CharSequence mLabel;

    /** A started activity; the task's back stack, bottom first. */
    private static final class Record {
        final Activity activity;
        final Record resultTo;
        final int requestCode;
        android.view.View decor;

        Record(Activity activity, Record resultTo, int requestCode) {
            this.activity = activity;
            this.resultTo = resultTo;
            this.requestCode = requestCode;
        }
    }

    private final java.util.ArrayList<Record> mStack = new java.util.ArrayList<Record>();

    private ActivityThread() {
        mH = new Handler(Looper.getMainLooper());
    }

    public static ActivityThread currentActivityThread() {
        return sCurrentActivityThread;
    }

    public static Application currentApplication() {
        ActivityThread t = sCurrentActivityThread;
        return t != null ? t.mApplication : null;
    }

    public static String currentPackageName() {
        ActivityThread t = sCurrentActivityThread;
        return t != null && t.mInfo != null ? t.mInfo.packageName : null;
    }

    Application getApplication() {
        return mApplication;
    }

    /**
     * args: package, application class ("" for android.app.Application),
     * launcher activity class, label, APK path, data directory, targetSdk,
     * minSdk (all guest values).
     */
    public static void main(String[] args) throws Exception {
        if (args == null || args.length < 8) throw new IllegalArgumentException("bad launch arguments");
        Looper.prepareMainLooper();
        ActivityThread thread = new ActivityThread();
        sCurrentActivityThread = thread;
        thread.bindApplication(args);
        thread.launchActivity(args[2]);
        thread.watchForStopRequest();
        Looper.loop();
        Log.i(TAG, "main looper ended; session over");
    }

    private void bindApplication(String[] args) throws Exception {
        ApplicationInfo info = new ApplicationInfo();
        info.packageName = args[0];
        info.processName = args[0];
        info.className = args[1].length() == 0 ? null : args[1];
        info.name = info.className;
        info.nonLocalizedLabel = args[3];
        info.sourceDir = args[4];
        info.publicSourceDir = args[4];
        info.dataDir = args[5];
        info.targetSdkVersion = Integer.parseInt(args[6]);
        info.minSdkVersion = Integer.parseInt(args[7]);
        info.uid = android.os.Process.myUid();
        info.nativeLibraryDir = System.getProperty("java.library.path");
        info.flags = ApplicationInfo.FLAG_HAS_CODE | ApplicationInfo.FLAG_INSTALLED;
        mInfo = info;
        mLabel = args[3];
        mClassLoader = nativeAppClassLoader();

        ContextImpl appContext = createContext();
        Application app;
        if (info.className == null) {
            app = new Application();
        } else {
            app = (Application) mClassLoader.loadClass(info.className).newInstance();
        }
        app.attach(appContext);
        mApplication = app;
        app.onCreate();
    }

    private ContextImpl createContext() {
        Resources system = Resources.getSystem();
        AssetManager assets = new AssetManager();
        Resources res = new Resources(assets, system.getDisplayMetrics(), system.getConfiguration());
        return new ContextImpl(this, mInfo, mClassLoader, assets, res);
    }

    private void launchActivity(String className) throws Exception {
        ComponentName component = new ComponentName(mInfo.packageName, className);
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_LAUNCHER);
        intent.setComponent(component);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        performLaunch(intent, null, -1);
    }

    private Record top() {
        return mStack.isEmpty() ? null : mStack.get(mStack.size() - 1);
    }

    private Record find(Activity activity) {
        for (Record r : mStack) {
            if (r.activity == activity) return r;
        }
        return null;
    }

    /**
     * Starts an activity of this package on top of the task (caller null for
     * a non-activity context). Other packages' activities do not exist here.
     */
    void startActivity(Activity caller, Intent intent, final int requestCode) {
        android.content.pm.ResolveInfo ri = mApplication.getPackageManager().resolveActivity(intent,
                intent.getComponent() == null ? android.content.pm.PackageManager.MATCH_DEFAULT_ONLY : 0);
        if (ri == null || ri.activityInfo == null) {
            throw new android.content.ActivityNotFoundException("No Activity found to handle " + intent);
        }
        final Intent launch = new Intent(intent);
        launch.setComponent(new ComponentName(mInfo.packageName, ri.activityInfo.name));
        final Record resultTo = caller != null && requestCode >= 0 ? find(caller) : null;
        mH.post(new Runnable() {
            public void run() {
                try {
                    performLaunch(launch, resultTo, requestCode);
                } catch (Exception e) {
                    throw new RuntimeException("Unable to start activity " + launch.getComponent(), e);
                }
            }
        });
    }

    /** Android's order: caller paused, new activity created/started/resumed, caller stopped. */
    private void performLaunch(Intent intent, Record resultTo, int requestCode) throws Exception {
        Record caller = top();
        if (caller != null && caller.activity.mResumed) {
            caller.activity.onWindowFocusChanged(false);
            caller.activity.performPause();
        }
        ComponentName component = intent.getComponent();
        Activity activity = (Activity) mClassLoader.loadClass(component.getClassName()).newInstance();
        activity.attach(createContext(), this, mApplication, intent, component, mLabel);
        if (resultTo != null) activity.mCallingActivity = resultTo.activity.getComponentName();
        Record r = new Record(activity, resultTo, requestCode);
        mStack.add(r);
        activity.performCreate(null);
        activity.performStart();
        activity.performResume();
        if (!activity.mFinished) makeVisible(r);
        if (caller != null && !caller.activity.mStopped && !caller.activity.mDestroyed) caller.activity.performStop();
    }

    private void makeVisible(final Record r) {
        final Activity activity = r.activity;
        PhoneWindow window = (PhoneWindow) activity.getWindow();
        android.view.WindowManager.LayoutParams attrs = window.getAttributes();
        attrs.setTitle(window.getTitle());
        activity.getWindowManager().addView(window.getDecorView(), attrs);
        r.decor = window.getDecorView();
        mH.post(new Runnable() {
            public void run() {
                if (!activity.mFinished && activity.mResumed) activity.onWindowFocusChanged(true);
            }
        });
    }

    void onActivityTitleChanged(Activity activity, CharSequence title) {
        Record top = top();
        if (top != null && activity == top.activity) android.view.WindowManagerGlobal.getInstance().setHostTitle(title);
    }

    /**
     * The host asks the session to end (its window or the launcher closed):
     * the activity leaves like it would for the home screen, then the main
     * looper ends.
     */
    private void watchForStopRequest() {
        Thread watcher = new Thread(new Runnable() {
            public void run() {
                nativeWaitForStopRequest();
                mH.post(new Runnable() {
                    public void run() {
                        Log.i(TAG, "session end requested");
                        if (mStack.isEmpty()) {
                            Looper.shutdownMainLooperForSessionEnd();
                            return;
                        }
                        // Top first; the last one ends the session.
                        while (!mStack.isEmpty()) destroyActivity(top().activity);
                    }
                });
            }
        }, "AXRB.SessionEnd");
        watcher.setDaemon(true);
        watcher.start();
    }

    void scheduleFinish(final Activity activity) {
        mH.post(new Runnable() {
            public void run() {
                destroyActivity(activity);
            }
        });
    }

    private void destroyActivity(Activity activity) {
        Record r = find(activity);
        if (r == null || activity.mDestroyed) return;
        boolean wasTop = r == top();
        if (activity.mResumed) {
            activity.onWindowFocusChanged(false);
            activity.performPause();
        }
        mStack.remove(r);
        Record next = wasTop ? top() : null;
        if (next != null && !next.activity.mDestroyed) {
            // The activity below comes back, with the finished one's result.
            Activity below = next.activity;
            if (below.mStopped) {
                below.performRestart();
                below.performStart();
            }
            if (r.resultTo == next && r.requestCode >= 0) {
                below.dispatchActivityResult(r.requestCode, activity.getResultCode(), activity.getResultData());
            }
            below.performResume();
            if (!below.mFinished) below.onWindowFocusChanged(true);
        }
        if (!activity.mStopped) activity.performStop();
        if (r.decor != null) {
            activity.getWindowManager().removeViewImmediate(r.decor);
            r.decor = null;
        }
        activity.performDestroy();
        if (mStack.isEmpty()) {
            // The task is empty: Android would leave a cached process behind;
            // for a desktop session this is the end.
            Looper.shutdownMainLooperForSessionEnd();
        }
    }

    /** The runtime-created PathClassLoader over the application's APK. */
    private static native ClassLoader nativeAppClassLoader();

    /** Blocks until the host requests the end of the session. */
    private static native void nativeWaitForStopRequest();
}
