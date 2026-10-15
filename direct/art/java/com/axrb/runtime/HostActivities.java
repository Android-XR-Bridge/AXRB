package com.axrb.runtime;

import android.os.IBinder;
import android.os.Process;
import android.content.Intent;

/** Single foreground application lifecycle exposed through AOSP service APIs. */
final class HostActivities {
    static final java.util.Map<IBinder, Integer> orientations = new java.util.concurrent.ConcurrentHashMap<>();
    static void install(HostServices services) throws Exception {
        IBinder client = services.register("axrb.activity_client", "android.app.IActivityClientController", (method, args) -> {
            switch (method.getName()) {
                case "getDisplayId": return 0;
                case "finishActivity": return AppProcess.finishActivity((IBinder) args[0], (Integer) args[1], (Intent) args[2]);
                case "getTaskForActivity": return 1;
                case "getRequestedOrientation": return orientations.getOrDefault((IBinder) args[0], -1);
                case "setRequestedOrientation": orientations.put((IBinder) args[0], (Integer) args[1]); return null;
                case "getLaunchedFromUid": return Process.myUid();
                case "getLaunchedFromPackage": return "com.axrb.launcher";
                case "getCallingPackage": case "getCallingActivity": return null;
                case "isTopOfTask": case "isRequestedToLaunchInTaskFragment": return false;
                case "activityIdle": case "activityResumed": case "activityPaused":
                case "activityStopped": case "activityDestroyed": case "reportActivityFullyDrawn":
                case "setTaskDescription": case "setShowWhenLocked":
                case "setTurnScreenOn": case "setInheritShowWhenLocked": case "setShouldDockBigOverlays":
                case "overridePendingTransition": case "overrideActivityTransition": case "clearOverrideActivityTransition":
                    return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        services.register("activity_task", "android.app.IActivityTaskManager", (method, args) -> {
            switch (method.getName()) {
                case "getActivityClientController": return client.queryLocalInterface("android.app.IActivityClientController");
                case "startActivity":
                    // IActivityTaskManager: caller, callingPackage, featureId,
                    // intent, resolvedType, resultTo, resultWho, requestCode...
                    Intent intent = (Intent) args[3];
                    if ("android.content.pm.action.REQUEST_PERMISSIONS".equals(intent.getAction())
                            && "com.axrb.permissioncontroller".equals(intent.getPackage()))
                        return AppProcess.requestPermissions(intent, (IBinder) args[5], (String) args[6],
                                (Integer) args[7], services.packages);
                    return AppProcess.startActivity((Intent) args[3], (IBinder) args[5], (String) args[6], (Integer) args[7]);
                case "getTaskForActivity": return 1;
                case "getDeviceConfigurationInfo": return new android.content.pm.ConfigurationInfo();
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        services.register("activity", "android.app.IActivityManager", (method, args) -> {
            switch (method.getName()) {
                case "registerReceiverWithFeature": case "registerReceiver": return HostBroadcasts.register(args);
                case "unregisterReceiver": HostBroadcasts.unregister(args[0]); return null;
                case "broadcastIntentWithFeature": case "broadcastIntent":
                    for (Object arg : args) if (arg instanceof Intent) { HostBroadcasts.send((Intent)arg); break; }
                    return 0;
                case "bindServiceInstance": {
                    Intent intent = null;
                    Object connection = null;
                    Class<?> connectionType = Class.forName("android.app.IServiceConnection");
                    for (Object arg : args) {
                        if (arg instanceof Intent) intent = (Intent) arg;
                        if (arg != null && connectionType.isInstance(arg)) connection = arg;
                    }
                    return HostAppServices.bind(services, intent, connection);
                }
                case "unbindService": return HostAppServices.unbind(args[0]);
                case "getContentProvider":
                    for (Object arg : args) if (services.providers.containsKey(arg)) return services.providers.get(arg);
                    return null;
                case "refContentProvider": return true;
                case "removeContentProvider": return null;
                case "getCurrentUserId": return 0;
                case "getRunningAppProcesses": {
                    android.content.pm.PackageInfo app = services.packages.installed;
                    if (app == null) return java.util.Collections.emptyList();
                    android.app.ActivityManager.RunningAppProcessInfo info =
                            new android.app.ActivityManager.RunningAppProcessInfo(
                                    app.applicationInfo.processName, Process.myPid(), new String[]{app.packageName});
                    info.uid = Process.myUid();
                    info.importance = android.app.ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
                    return java.util.Collections.singletonList(info);
                }
                case "getMemoryInfo": {
                    android.app.ActivityManager.MemoryInfo memory = (android.app.ActivityManager.MemoryInfo) args[0];
                    try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader("/proc/meminfo"))) {
                        String line;
                        while ((line = reader.readLine()) != null) {
                            String[] words = line.trim().split("\\s+");
                            if (words[0].equals("MemTotal:")) memory.totalMem = Long.parseLong(words[1]) * 1024;
                            if (words[0].equals("MemAvailable:")) memory.availMem = Long.parseLong(words[1]) * 1024;
                        }
                    }
                    memory.threshold = Math.min(memory.totalMem / 10, 256L * 1024 * 1024);
                    memory.lowMemory = memory.availMem < memory.threshold;
                    return null;
                }
                case "getProcessMemoryInfo": {
                    int[] pids = (int[]) args[0];
                    android.os.Debug.MemoryInfo[] result = new android.os.Debug.MemoryInfo[pids.length];
                    for (int i = 0; i < pids.length; ++i) {
                        result[i] = new android.os.Debug.MemoryInfo();
                        if (pids[i] == Process.myPid()) android.os.Debug.getMemoryInfo(result[i]);
                    }
                    return result;
                }
                case "getUidProcessState": return 2;
                case "isUserAMonkey": case "isBackgroundRestricted": return false;
                case "isUserRunning": return true;
                case "checkPermission": case "checkPermissionForDevice": return services.packages.permission((String) args[0]);
                case "finishReceiver": case "attachApplication": case "setProcessImportant": return null;
                // Loading another package's code (com.oculus.systemdriver) records a
                // dependency for the system to restart this process if it changes.
                case "addPackageDependency": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
    }
}
