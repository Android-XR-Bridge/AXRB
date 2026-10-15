package android.app;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Configuration;
import android.os.Bundle;
import java.util.ArrayList;

public class Application extends ContextWrapper implements android.content.ComponentCallbacks2 {
    public interface ActivityLifecycleCallbacks {
        void onActivityCreated(Activity activity, Bundle savedInstanceState);

        void onActivityStarted(Activity activity);

        void onActivityResumed(Activity activity);

        void onActivityPaused(Activity activity);

        void onActivityStopped(Activity activity);

        void onActivitySaveInstanceState(Activity activity, Bundle outState);

        void onActivityDestroyed(Activity activity);
    }

    private final ArrayList<ActivityLifecycleCallbacks> mCallbacks = new ArrayList<ActivityLifecycleCallbacks>();

    public Application() {
        super(null);
    }

    /** Called by ActivityThread before onCreate. */
    final void attach(Context context) {
        attachBaseContext(context);
    }

    public void onCreate() {}

    public void onTerminate() {}

    public void onConfigurationChanged(Configuration newConfig) {}

    public void onLowMemory() {}

    public void onTrimMemory(int level) {}

    /** Configuration and memory events are never sent in this runtime. */
    public void registerComponentCallbacks(android.content.ComponentCallbacks callback) {}

    public void unregisterComponentCallbacks(android.content.ComponentCallbacks callback) {}

    public void registerActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
        synchronized (mCallbacks) {
            mCallbacks.add(callback);
        }
    }

    public void unregisterActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
        synchronized (mCallbacks) {
            mCallbacks.remove(callback);
        }
    }

    private ActivityLifecycleCallbacks[] callbacks() {
        synchronized (mCallbacks) {
            return mCallbacks.toArray(new ActivityLifecycleCallbacks[0]);
        }
    }

    void dispatchActivityCreated(Activity activity, Bundle savedInstanceState) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityCreated(activity, savedInstanceState);
    }

    void dispatchActivityStarted(Activity activity) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityStarted(activity);
    }

    void dispatchActivityResumed(Activity activity) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityResumed(activity);
    }

    void dispatchActivityPaused(Activity activity) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityPaused(activity);
    }

    void dispatchActivityStopped(Activity activity) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityStopped(activity);
    }

    void dispatchActivityDestroyed(Activity activity) {
        for (ActivityLifecycleCallbacks c : callbacks()) c.onActivityDestroyed(activity);
    }
}
