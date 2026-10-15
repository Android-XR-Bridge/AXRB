package com.axrb.runtime;

import android.app.Service;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import java.util.*;

/** Runs APK-declared Android Services and delivers standard binding callbacks. */
final class HostAppServices {
    private static final Map<ComponentName, Service> running = new HashMap<>();
    private static final Map<Object, Service> connections = new HashMap<>();

    static int bind(HostServices platform, Intent intent, Object connection) {
        ServiceInfo info = resolve(platform, intent);
        if (info == null) return 0;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Service service = create(platform, info);
                IBinder binder = service.onBind(intent);
                connections.put(connection, service);
                Reflect.call(connection, "connected", new ComponentName(info.packageName, info.name), binder, false);
            } catch (Exception error) { throw new RuntimeException("Service binding failed: " + info.name, error); }
        });
        return 1;
    }
    static boolean unbind(Object connection) { return connections.remove(connection) != null; }
    static ServiceInfo resolve(HostServices platform, Intent intent) {
        if (intent.getComponent() == null || platform.packages.installed.services == null) return null;
        for (ServiceInfo info : platform.packages.installed.services)
            if (info.name.equals(intent.getComponent().getClassName()) && info.packageName.equals(intent.getComponent().getPackageName())) return info;
        return null;
    }
    private static Service create(HostServices platform, ServiceInfo info) throws Exception {
        ComponentName name = new ComponentName(info.packageName, info.name);
        Service service = running.get(name);
        if (service != null) return service;
        Object thread = Reflect.call(Class.forName("android.app.ActivityThread"), "currentActivityThread");
        android.app.Application application = (android.app.Application) Reflect.field(thread, "mInitialApplication");
        Context context = application.createPackageContext(info.packageName, Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY);
        service = (Service) context.getClassLoader().loadClass(info.name).getDeclaredConstructor().newInstance();
        Reflect.call(service, "attach", context, thread, info.name, new Binder(), application,
                Reflect.call(Class.forName("android.app.ActivityManager"), "getService"));
        running.put(name, service);
        service.onCreate();
        return service;
    }
}
