package com.axrb.runtime;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Process;
import java.lang.reflect.Method;
import java.util.Collections;

/** Package metadata for the APK installed into this isolated Direct session. */
final class HostPackageManager implements HostServices.Call {
    PackageInfo installed;
    /** com.oculus.systemdriver for older Oculus loaders, when the session offers it. */
    PackageInfo legacyDriver;
    String launcherActivity;
    final java.util.Map<String, android.content.pm.ProviderInfo> providers = new java.util.HashMap<>();
    private final java.util.List<android.content.pm.FeatureInfo> features = new java.util.ArrayList<>();
    HostPackageManager() {
        feature("android.hardware.vr.headtracking", 1);
        feature("android.hardware.vulkan.level", 1);
        feature("android.hardware.vulkan.version", 0x401000);
    }
    private void feature(String name, int version) {
        android.content.pm.FeatureInfo info = new android.content.pm.FeatureInfo();
        info.name = name;
        info.version = version;
        features.add(info);
    }
    int permission(String permission) {
        // Only capabilities exposed by the Direct session can be granted.
        if (!(permission.equals("android.permission.INTERNET") ||
              permission.equals("android.permission.ACCESS_NETWORK_STATE") ||
              permission.equals("android.permission.ACCESS_WIFI_STATE") ||
              permission.equals("android.permission.WAKE_LOCK") ||
              permission.equals("android.permission.READ_EXTERNAL_STORAGE") ||
              permission.equals("android.permission.WRITE_EXTERNAL_STORAGE") ||
              permission.equals("android.permission.MODIFY_AUDIO_SETTINGS") ||
              permission.equals("android.permission.VIBRATE"))) return PackageManager.PERMISSION_DENIED;
        if (installed != null && installed.requestedPermissions != null)
            for (String declared : installed.requestedPermissions) if (permission.equals(declared)) return PackageManager.PERMISSION_GRANTED;
        return PackageManager.PERMISSION_DENIED;
    }
    private boolean known(Object value) { return installed != null && installed.packageName.equals(value); }
    private PackageInfo lookup(Object value) {
        if (known(value)) return installed;
        return legacyDriver != null && legacyDriver.packageName.equals(value) ? legacyDriver : null;
    }
    private java.util.List<PackageInfo> all() {
        java.util.List<PackageInfo> result = new java.util.ArrayList<>();
        if (installed != null) result.add(installed);
        if (legacyDriver != null) result.add(legacyDriver);
        return result;
    }
    /** Registers the adapter APK mounted at path as an installed system package. */
    void offerLegacyDriver(android.content.Context context, String path) {
        PackageInfo info = context.getPackageManager().getPackageArchiveInfo(path, 0);
        if (info == null) throw new IllegalArgumentException("Cannot parse legacy Oculus driver " + path);
        ApplicationInfo app = info.applicationInfo;
        app.packageName = info.packageName;
        app.sourceDir = app.publicSourceDir = path;
        app.nativeLibraryDir = new java.io.File(path).getParent() + "/lib/arm64";
        app.dataDir = "/data/data/" + info.packageName;
        app.deviceProtectedDataDir = app.dataDir;
        app.uid = Process.myUid();
        app.flags |= ApplicationInfo.FLAG_SYSTEM | ApplicationInfo.FLAG_INSTALLED | ApplicationInfo.FLAG_HAS_CODE;
        app.enabled = true;
        try {
            Reflect.set(app, "credentialProtectedDataDir", app.dataDir);
            Reflect.set(app, "primaryCpuAbi", "arm64-v8a");
        } catch (Exception error) { throw new IllegalStateException(error); }
        legacyDriver = info;
    }
    public Object invoke(Method method, Object[] args) throws Throwable {
        switch (method.getName()) {
            case "resolveContentProvider": return providers.get(args[0]);
            case "queryIntentActivities": {
                android.content.Intent intent = (android.content.Intent) args[0];
                java.util.List<android.content.pm.ResolveInfo> matches = new java.util.ArrayList<>();
                if (installed != null && installed.activities != null) for (android.content.pm.ActivityInfo activity : installed.activities) {
                    boolean explicit = intent.getComponent() != null && intent.getComponent().getPackageName().equals(installed.packageName)
                            && intent.getComponent().getClassName().equals(activity.name);
                    boolean launcher = android.content.Intent.ACTION_MAIN.equals(intent.getAction()) && activity.name.equals(launcherActivity)
                            && (intent.getPackage() == null || intent.getPackage().equals(installed.packageName));
                    if (explicit || launcher) { android.content.pm.ResolveInfo result = new android.content.pm.ResolveInfo(); result.activityInfo = activity; matches.add(result); }
                }
                return Reflect.create("android.content.pm.ParceledListSlice", matches);
            }
            case "getActivityInfo": case "getReceiverInfo": {
                android.content.ComponentName component = (android.content.ComponentName) args[0];
                android.content.pm.ActivityInfo[] entries = installed == null ? null : method.getName().equals("getActivityInfo") ? installed.activities : installed.receivers;
                if (entries != null) for (android.content.pm.ActivityInfo entry : entries)
                    if (entry.packageName.equals(component.getPackageName()) && entry.name.equals(component.getClassName())) return entry;
                return null;
            }
            case "getServiceInfo": {
                android.content.ComponentName component = (android.content.ComponentName) args[0];
                if (installed != null && installed.services != null) for (android.content.pm.ServiceInfo entry : installed.services)
                    if (entry.packageName.equals(component.getPackageName()) && entry.name.equals(component.getClassName())) return entry;
                return null;
            }
            case "getPackageInfo": case "getPackageInfoVersioned": return lookup(args[0]);
            case "getApplicationInfo": { PackageInfo info = lookup(args[0]); return info == null ? null : info.applicationInfo; }
            case "getInstalledApplications": {
                java.util.List<ApplicationInfo> apps = new java.util.ArrayList<>();
                for (PackageInfo info : all()) apps.add(info.applicationInfo);
                return Reflect.create("android.content.pm.ParceledListSlice", apps);
            }
            case "getInstalledPackages": return Reflect.create("android.content.pm.ParceledListSlice", all());
            case "getPackageUid": return lookup(args[0]) != null ? Process.myUid() : -1;
            case "getPackagesForUid": return installed == null ? new String[0] : new String[]{installed.packageName};
            case "getNameForUid": return installed == null ? "android" : installed.packageName;
            case "isPackageAvailable": return lookup(args[0]) != null;
            case "hasSystemFeature":
                for (android.content.pm.FeatureInfo feature : features)
                    if (feature.name.equals(args[0]) && feature.version >= (Integer) args[1]) return true;
                return false;
            case "getSystemAvailableFeatures": return Reflect.create("android.content.pm.ParceledListSlice", features);
            case "getSystemSharedLibraryNames": return new String[0];
            case "getSharedLibraries": return Reflect.create("android.content.pm.ParceledListSlice", Collections.emptyList());
            case "getTargetSdkVersion": return installed == null ? 36 : installed.applicationInfo.targetSdkVersion;
            case "getApplicationEnabledSetting": case "getComponentEnabledSetting": return PackageManager.COMPONENT_ENABLED_STATE_DEFAULT;
            case "getInstallerPackageName": return null;
            case "getInstallSourceInfo":
                if (!known(args[0])) return null;
                // A local APK has no Android installer package or signing identity.
                return Reflect.create("android.content.pm.InstallSourceInfo", null, null, null, null,
                        null, android.content.pm.PackageInstaller.PACKAGE_SOURCE_LOCAL_FILE);
            case "checkPermission": return permission((String) args[0]);
            case "getPermissionControllerPackageName": return "com.axrb.permissioncontroller";
            case "getSdkSandboxPackageName": return "com.android.sdksandbox";
            case "isInstantApp": case "isAppArchivingEnabled": return false;
            case "notifyPackageUse": case "notifyDexLoad": return null;
            default: throw new UnsupportedOperationException(method.getName());
        }
    }
}
