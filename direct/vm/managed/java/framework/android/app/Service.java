package android.app;

import android.content.ComponentCallbacks2;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.IBinder;

/**
 * Services are never started in this runtime (startService/bindService
 * report failure), so this class only has to exist for apps to link.
 */
public abstract class Service extends ContextWrapper implements ComponentCallbacks2 {
    public static final int START_CONTINUATION_MASK = 0xf;
    public static final int START_STICKY_COMPATIBILITY = 0;
    public static final int START_STICKY = 1;
    public static final int START_NOT_STICKY = 2;
    public static final int START_REDELIVER_INTENT = 3;
    public static final int START_FLAG_REDELIVERY = 1;
    public static final int START_FLAG_RETRY = 2;
    public static final int STOP_FOREGROUND_REMOVE = 1;
    public static final int STOP_FOREGROUND_DETACH = 2;

    private Application mApplication;

    public Service() {
        super(null);
    }

    public final Application getApplication() {
        return mApplication;
    }

    public void onCreate() {}

    @Deprecated
    public void onStart(Intent intent, int startId) {}

    public int onStartCommand(Intent intent, int flags, int startId) {
        onStart(intent, startId);
        return START_STICKY;
    }

    public void onDestroy() {}

    public void onConfigurationChanged(Configuration newConfig) {}

    public void onLowMemory() {}

    public void onTrimMemory(int level) {}

    public abstract IBinder onBind(Intent intent);

    public boolean onUnbind(Intent intent) {
        return false;
    }

    public void onRebind(Intent intent) {}

    public void onTaskRemoved(Intent rootIntent) {}

    public final void stopSelf() {}

    public final void stopSelf(int startId) {}

    public final boolean stopSelfResult(int startId) {
        return false;
    }

    public final void startForeground(int id, Notification notification) {}

    public final void startForeground(int id, Notification notification, int foregroundServiceType) {}

    public final void stopForeground(boolean removeNotification) {}

    public final void stopForeground(int flags) {}
}
