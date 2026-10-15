package android.net;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

/** The host network, reported as an always-connected Wi-Fi network. */
public class ConnectivityManager {
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_ETHERNET = 9;
    public static final String CONNECTIVITY_ACTION = "android.net.conn.CONNECTIVITY_CHANGE";

    private static final Network sNetwork = new Network(100);

    public static class NetworkCallback {
        public NetworkCallback() {}

        public void onAvailable(Network network) {}

        public void onLosing(Network network, int maxMsToLive) {}

        public void onLost(Network network) {}

        public void onUnavailable() {}

        public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {}

        public void onLinkPropertiesChanged(Network network, Object linkProperties) {}
    }

    public ConnectivityManager(Context context) {}

    public NetworkInfo getActiveNetworkInfo() {
        return new NetworkInfo();
    }

    public NetworkInfo getNetworkInfo(int networkType) {
        return networkType == TYPE_WIFI ? new NetworkInfo() : null;
    }

    public NetworkInfo getNetworkInfo(Network network) {
        return new NetworkInfo();
    }

    public Network getActiveNetwork() {
        return sNetwork;
    }

    public Network[] getAllNetworks() {
        return new Network[] {sNetwork};
    }

    public NetworkCapabilities getNetworkCapabilities(Network network) {
        return new NetworkCapabilities();
    }

    public boolean isActiveNetworkMetered() {
        return false;
    }

    public void registerDefaultNetworkCallback(final NetworkCallback callback) {
        registerDefaultNetworkCallback(callback, new Handler(Looper.getMainLooper()));
    }

    public void registerDefaultNetworkCallback(final NetworkCallback callback, Handler handler) {
        handler.post(new Runnable() {
            public void run() {
                callback.onAvailable(sNetwork);
                callback.onCapabilitiesChanged(sNetwork, new NetworkCapabilities());
            }
        });
    }

    public void registerNetworkCallback(NetworkRequest request, NetworkCallback callback) {
        registerDefaultNetworkCallback(callback);
    }

    public void unregisterNetworkCallback(NetworkCallback callback) {}
}
