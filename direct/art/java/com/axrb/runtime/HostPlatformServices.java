package com.axrb.runtime;

import android.os.Bundle;
import java.util.Collections;

/** Session-level platform state that has no separate Android system process. */
final class HostPlatformServices {
    static void install(HostServices services) throws Exception {
        android.net.Network network = (android.net.Network) Reflect.create("android.net.Network", 100);
        android.net.NetworkCapabilities networkCapabilities = new android.net.NetworkCapabilities();
        Reflect.call(networkCapabilities, "addTransportType", android.net.NetworkCapabilities.TRANSPORT_ETHERNET);
        for (int capability : new int[]{12, 16, 11}) Reflect.call(networkCapabilities, "addCapability", capability);
        android.net.LinkProperties networkLinks = new android.net.LinkProperties();
        networkLinks.setInterfaceName("axrb0");
        android.net.NetworkInfo networkInfo = new android.net.NetworkInfo(9, 0, "ETHERNET", "");
        networkInfo.setDetailedState(android.net.NetworkInfo.DetailedState.CONNECTED, null, "AXRB host");
        java.util.concurrent.atomic.AtomicInteger nextNetworkRequest = new java.util.concurrent.atomic.AtomicInteger(1);
        java.util.Set<Object> networkRequests = java.util.concurrent.ConcurrentHashMap.newKeySet();
        services.register("connectivity", "android.net.IConnectivityManager", (method, args) -> {
            switch (method.getName()) {
                case "getEnabledConnectivityManagerFeatures": return 0L;
                case "listenForNetwork": case "requestNetwork": {
                    boolean listen = method.getName().equals("listenForNetwork");
                    android.net.NetworkCapabilities requested = (android.net.NetworkCapabilities) args[listen ? 0 : 1];
                    android.os.Messenger messenger = (android.os.Messenger) args[listen ? 1 : 3];
                    Class<?> requestTypes = Class.forName("android.net.NetworkRequest$Type");
                    Object type = listen ? Reflect.field(requestTypes, "LISTEN") : requestTypes.getEnumConstants()[(Integer) args[2]];
                    android.net.NetworkRequest request = (android.net.NetworkRequest) Reflect.create(
                            "android.net.NetworkRequest", requested == null ? networkCapabilities : requested,
                            listen ? -1 : (Integer) args[6], nextNetworkRequest.getAndIncrement(), type);
                    networkRequests.add(request);
                    // AOSP registers the returned request before dispatching this Messenger event.
                    new android.os.Handler(android.os.Looper.getMainLooper()).post(() -> {
                        if (!networkRequests.contains(request)) return;
                        try {
                            if (requested != null && !(Boolean) Reflect.call(requested, "satisfiedByNetworkCapabilities", networkCapabilities)) return;
                            android.os.Message message = android.os.Message.obtain();
                            message.what = (Integer) Reflect.field(android.net.ConnectivityManager.class, "CALLBACK_AVAILABLE");
                            Bundle data = new Bundle();
                            data.putParcelable("NetworkRequest", request);
                            data.putParcelable("Network", network);
                            data.putParcelable("NetworkCapabilities", new android.net.NetworkCapabilities(networkCapabilities));
                            data.putParcelable("LinkProperties", networkLinks);
                            message.setData(data);
                            messenger.send(message);
                        } catch (Exception e) { throw new IllegalStateException("Network callback delivery failed", e); }
                    });
                    return request;
                }
                case "getActiveNetwork": case "getActiveNetworkForUid": return network;
                case "getAllNetworks": return new android.net.Network[]{network};
                case "getActiveNetworkInfo": case "getActiveNetworkInfoForUid": case "getNetworkInfo": case "getNetworkInfoForUid": return networkInfo;
                case "getAllNetworkInfo": return new android.net.NetworkInfo[]{networkInfo};
                case "getNetworkCapabilities": return new android.net.NetworkCapabilities(networkCapabilities);
                case "getLinkProperties": case "getActiveLinkProperties": return Reflect.create("android.net.LinkProperties", networkLinks);
                case "isActiveNetworkMetered": return false;
                case "getRestrictBackgroundStatus": return 1;
                case "getDefaultProxy": case "getProxyForNetwork": return null;
                case "releaseNetworkRequest": networkRequests.remove(args[0]); return null;
                case "reportNetworkConnectivity": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        java.util.Set<Object> wakeLocks = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
        services.register("power", "android.os.IPowerManager", (method, args) -> {
            switch (method.getName()) {
                case "isInteractive": case "isWakeLockLevelSupported": return true;
                case "isPowerSaveMode": case "isDeviceIdleMode": case "isLightDeviceIdleMode": return false;
                case "acquireWakeLock": case "acquireWakeLockAsync": wakeLocks.add(args[0]); return null;
                case "releaseWakeLock": case "releaseWakeLockAsync": wakeLocks.remove(args[0]); return null;
                case "updateWakeLockUids": case "updateWakeLockWorkSource": case "userActivity": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        java.util.Set<Object> thermalListeners = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
        services.register("thermalservice", "android.os.IThermalService", (method, args) -> {
            switch (method.getName()) {
                case "registerThermalStatusListener": thermalListeners.add(args[0]);
                    Reflect.call(args[0], "onStatusChange", 0); return true;
                case "unregisterThermalStatusListener": return thermalListeners.remove(args[0]);
                case "getCurrentThermalStatus": return 0;
                case "getThermalHeadroom": return Float.NaN;
                case "getCurrentTemperatures": case "getCurrentCoolingDevices":
                    return java.lang.reflect.Array.newInstance(method.getReturnType().getComponentType(), 0);
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        Object volume = Reflect.create("android.os.storage.StorageVolume", "emulated;0",
                new java.io.File("/storage/emulated/0"), new java.io.File("/storage/emulated/0"), "AXRB storage",
                true, false, true, false, false, 0L, Reflect.create("android.os.UserHandle", 0), null, null, "mounted");
        Object storageVolumes = java.lang.reflect.Array.newInstance(volume.getClass(), 1);
        java.lang.reflect.Array.set(storageVolumes, 0, volume);
        services.register("mount", "android.os.storage.IStorageManager", (method, args) -> {
            switch (method.getName()) {
                case "getVolumeList": return storageVolumes;
                case "isUserKeyUnlocked": case "isExternalStorageEmulated": return true;
                case "isExternalStorageManager": return false;
                case "registerListener": case "unregisterListener": return null;
                case "getAllocatableBytes": return new java.io.File("/storage/emulated/0").getUsableSpace();
                case "fixupAppDir":
                    // The session's emulated storage already uses the app UID;
                    // there is no separate FUSE daemon to adjust ownership.
                    String appPath = (String) args[0];
                    if (!new java.io.File(appPath).getCanonicalPath().startsWith("/storage/emulated/0/Android/"))
                        throw new SecurityException(appPath);
                    if (!new java.io.File(appPath).isDirectory() && !new java.io.File(appPath).mkdirs())
                        throw new java.io.IOException("Cannot create app storage " + appPath);
                    return null;
                case "mkdirs":
                    String path = (String) args[args.length - 1];
                    if (!path.startsWith("/storage/emulated/0/Android/")) throw new SecurityException(path);
                    new java.io.File(path).mkdirs(); return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        java.util.Set<Object> inputClients = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
        services.register("input_method", "com.android.internal.view.IInputMethodManager", (method, args) -> {
            switch (method.getName()) {
                case "addClient": inputClients.add(args[0]); return null;
                case "getCurrentInputMethodInfoAsUser": case "getLastInputMethodSubtype": return null;
                case "getInputMethodList": case "getEnabledInputMethodList":
                    if (java.util.List.class.isAssignableFrom(method.getReturnType())) return Collections.emptyList();
                    return Reflect.call(method.getReturnType(), "create", Collections.emptyList());
                case "getEnabledInputMethodSubtypeList": return Collections.emptyList();
                case "isInputMethodPickerShownForTest": case "isStylusHandwritingAvailableAsUser":
                case "showSoftInput": case "hideSoftInput": return false;
                // Requests to show or hide the keyboard are tracked by this service
                // first; without one, Android uses a local token (no IME is installed).
                case "getImeTrackerService": return null;
                case "startInputOrWindowGainedFocus":
                    return Reflect.field(Class.forName("com.android.internal.inputmethod.InputBindResult"), "NO_IME");
                case "reportPerceptibleAsync": case "removeImeSurface": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        services.register("input", "android.hardware.input.IInputManager", (method, args) -> {
            switch (method.getName()) {
                case "getInputDeviceIds": return new int[0];
                case "getInputDevice": return null;
                case "getInputDevices": return new android.view.InputDevice[0];
                case "getVelocityTrackerStrategy": return "default";
                case "registerInputDevicesChangedListener": case "registerTabletModeChangedListener": return null;
                case "isInTabletMode": return 0;
                case "isMicMuted": return 1;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        java.util.Map<Integer, Integer> volumes = new java.util.concurrent.ConcurrentHashMap<>();
        java.util.concurrent.atomic.AtomicInteger nextPlayer = new java.util.concurrent.atomic.AtomicInteger(1);
        java.util.Map<Integer, Object> players = new java.util.concurrent.ConcurrentHashMap<>();
        services.register("audio", "android.media.IAudioService", (method, args) -> {
            switch (method.getName()) {
                case "trackPlayer": {
                    int id = nextPlayer.getAndIncrement();
                    players.put(id, args[0]);
                    return id;
                }
                case "releasePlayer": players.remove(args[0]); return null;
                case "playerAttributes": case "playerEvent": case "playerSessionId": return null;
                case "getStreamVolume": case "getLastAudibleStreamVolume": return volumes.getOrDefault((Integer) args[0], 10);
                case "getStreamMaxVolume": return 15;
                case "isMusicActive": return HostMedia.command(17, 0, 0, 0, 0) != 0;
                case "getStreamMinVolume": return 0;
                case "setStreamVolume": {
                    int stream = (Integer)args[0], before = volumes.getOrDefault(stream, 10);
                    int value = Math.max(0, Math.min(15, (Integer)args[1]));
                    volumes.put(stream, value);
                    if (before != value) HostBroadcasts.send(new android.content.Intent("android.media.VOLUME_CHANGED_ACTION")
                            .putExtra("android.media.EXTRA_VOLUME_STREAM_TYPE", stream)
                            .putExtra("android.media.EXTRA_VOLUME_STREAM_VALUE", value)
                            .putExtra("android.media.EXTRA_PREV_VOLUME_STREAM_VALUE", before));
                    return null;
                }
                case "isStreamMute": return volumes.getOrDefault((Integer) args[0], 10) == 0;
                case "isMasterMute": case "isMicrophoneMuted": case "isBluetoothScoOn": case "isBluetoothA2dpOn": case "isVolumeFixed": return false;
                case "getMode": return 0;
                case "getRingerModeExternal": case "getRingerModeInternal": return 2;
                case "requestAudioFocus": case "abandonAudioFocus": return 1;
                case "registerAudioFocusDispatcher": case "unregisterAudioFocusClient": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        java.util.Set<Object> observers = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
        services.register("content", "android.content.IContentService", (method, args) -> {
            switch (method.getName()) {
                case "registerContentObserver": observers.add(args[2]); return null;
                case "unregisterContentObserver": observers.remove(args[0]); return null;
                case "getMasterSyncAutomatically": case "isSyncActive": case "isSyncPending": return false;
                case "getCurrentSyncs": return Collections.emptyList();
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        Object owner = Reflect.create("android.content.pm.UserInfo", 0, "Owner", 0x13);
        services.register("user", "android.os.IUserManager", (method, args) -> {
            switch (method.getName()) {
                case "isUserUnlocked": case "isUserUnlockingOrUnlocked": case "isUserRunning": return (Integer) args[0] == 0;
                case "isManagedProfile": case "isQuietModeEnabled": case "hasUserRestriction":
                case "hasUserRestrictionOnAnyUser": case "isDemoUser": case "isPreCreated": return false;
                case "getUserInfo": return (Integer) args[0] == 0 ? owner : null;
                case "getUsers": case "getProfiles": return Collections.singletonList(owner);
                case "getProfileIds": return new int[]{0};
                case "getUserRestrictions": return new Bundle();
                case "getProfileParent": return null;
                case "getUserSerialNumber": case "getUserHandle": return (Integer) args[0] == 0 ? 0 : -1;
                case "getCredentialOwnerProfile": case "getProfileParentId": return 0;
                case "getUserName": return "Owner";
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
    }
}
