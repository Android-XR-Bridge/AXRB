package com.axrb.runtime;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import java.lang.reflect.*;
import java.util.*;

/** Android service interfaces backed by the Direct host, shared by all apps. */
final class HostServices {
    private final Map<String, IBinder> services = new HashMap<>();
    private NativeRegistry nativeRegistry;
    final HostPackageManager packages = new HostPackageManager();
    final Map<String, Object> providers = new HashMap<>();
    void attach(android.content.Context context) throws Exception {
        provider(context, "settings", new HostSettings());
        provider(context, "org.khronos.openxr.system_runtime_broker", new OpenXrProvider());
        provider(context, "org.khronos.openxr.runtime_broker", new OpenXrProvider());
    }
    private void provider(android.content.Context context, String authority, android.content.ContentProvider provider) throws Exception {
        android.content.pm.ProviderInfo info = new android.content.pm.ProviderInfo();
        info.authority = authority;
        info.name = provider.getClass().getName();
        info.packageName = "android";
        info.applicationInfo = context.getApplicationInfo();
        provider.attachInfo(context, info);
        Object holder = Reflect.create("android.app.ContentProviderHolder", info);
        Reflect.set(holder, "provider", Reflect.call(provider, "getIContentProvider"));
        Reflect.set(holder, "noReleaseNeeded", true);
        providers.put(authority, holder);
        packages.providers.put(authority, info);
    }
    interface Call { Object invoke(Method method, Object[] args) throws Throwable; }

    IBinder register(String name, String descriptor, Call call) throws Exception {
        Class<?> type = Class.forName(descriptor);
        Binder binder = new Binder();
        IInterface implementation = (IInterface) Proxy.newProxyInstance(type.getClassLoader(),
                new Class<?>[]{type}, (proxy, method, args) -> {
            if (method.getName().equals("asBinder")) return binder;
            if (method.getName().equals("toString")) return "AXRB " + descriptor;
            if (method.getName().equals("hashCode")) return System.identityHashCode(proxy);
            if (method.getName().equals("equals")) return proxy == args[0];
            try { return call.invoke(method, args == null ? new Object[0] : args); }
            catch (UnsupportedOperationException e) {
                System.err.println("AXRB_SERVICE_UNIMPLEMENTED " + descriptor + "." + method.getName());
                throw e;
            }
        });
        binder.attachInterface(implementation, descriptor);
        services.put(name, binder);
        return binder;
    }

    void install() throws Exception {
        IBinder registry = register("manager", "android.os.IServiceManager", (method, args) -> {
            switch (method.getName()) {
                case "getService": case "checkService": return services.get((String) args[0]);
                case "getService2": case "checkService2": {
                    Object metadata = Reflect.create("android.os.ServiceWithMetadata");
                    Reflect.set(metadata, "service", services.get((String) args[0]));
                    return Reflect.call(method.getReturnType(), "serviceWithMetadata", metadata);
                }
                case "addService": services.put((String) args[0], (IBinder) args[1]); return null;
                case "listServices": return services.keySet().toArray(new String[0]);
                case "isDeclared": return services.containsKey((String) args[0]);
                case "getDeclaredInstances": return new String[0];
                case "registerForNotifications": case "unregisterForNotifications": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        Reflect.set(Class.forName("android.os.ServiceManager"), "sServiceManager",
                registry.queryLocalInterface("android.os.IServiceManager"));
        installDisplay();
        register("package", "android.content.pm.IPackageManager", packages);
        register("permissionmgr", "android.permission.IPermissionManager", (method, args) -> {
            switch (method.getName()) {
                // Direct has no user-toggleable Android permissions. Its
                // capability policy permanently denies unsupported access.
                case "shouldShowRequestPermissionRationale": case "isPermissionRevokedByPolicy": return false;
                case "checkPermission": case "checkUidPermission": return packages.permission((String) args[0]);
                case "getPermissionFlags": return 0;
                case "getSplitPermissions": return Collections.emptyList();
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        HostActivities.install(this);
        register("notification", "android.app.INotificationManager", (method, args) -> {
            switch (method.getName()) {
                case "enqueueTextToast":
                    // There is no Android SystemUI in Direct mode. Preserve the
                    // diagnostic text and report that no toast was displayed.
                    System.out.println("AXRB_ANDROID_TOAST " + args[2]);
                    return false;
                case "areNotificationsEnabled": case "areNotificationsEnabledForPackage": return false;
                case "cancelToast": case "finishToken": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        HostPlatformServices.install(this);
        HostWindows.install(this);
        nativeRegistry = new NativeRegistry(services);
    }

    private void installDisplay() throws Exception {
        Object info = Reflect.create("android.view.DisplayInfo");
        for (String field : new String[]{"logicalWidth", "appWidth", "smallestNominalAppWidth", "largestNominalAppWidth"}) Reflect.set(info, field, 1920);
        for (String field : new String[]{"logicalHeight", "appHeight", "smallestNominalAppHeight", "largestNominalAppHeight"}) Reflect.set(info, field, 1080);
        Reflect.set(info, "name", "AXRB Host Display");
        Reflect.set(info, "uniqueId", "axrb:0");
        Reflect.set(info, "displayId", 0);
        Reflect.set(info, "logicalDensityDpi", 320);
        Reflect.set(info, "physicalXDpi", 320f);
        Reflect.set(info, "physicalYDpi", 320f);
        Reflect.set(info, "state", 2);
        Reflect.set(info, "type", 1);
        Reflect.set(info, "modeId", 1);
        Reflect.set(info, "defaultModeId", 1);
        Object mode = Reflect.create("android.view.Display$Mode", 1, 1920, 1080, 60f);
        Object modes = Array.newInstance(mode.getClass(), 1);
        Array.set(modes, 0, mode);
        Reflect.set(info, "supportedModes", modes);
        register("display", "android.hardware.display.IDisplayManager", (method, args) -> {
            switch (method.getName()) {
                case "getDisplayInfo": return ((Integer) args[0]) == 0 ? info : null;
                case "getDisplayIds": return new int[]{0};
                case "getWifiDisplayStatus": return Reflect.create("android.hardware.display.WifiDisplayStatus");
                case "registerCallback": case "registerCallbackWithEventMask": return null;
                case "getPreferredWideGamutColorSpaceId": return 0;
                case "getOverlaySupport": return Reflect.create("android.hardware.OverlayProperties", 0L);
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
    }
}
