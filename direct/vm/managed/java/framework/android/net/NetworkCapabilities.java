package android.net;

public final class NetworkCapabilities {
    public static final int TRANSPORT_CELLULAR = 0;
    public static final int TRANSPORT_WIFI = 1;
    public static final int TRANSPORT_BLUETOOTH = 2;
    public static final int TRANSPORT_ETHERNET = 3;
    public static final int NET_CAPABILITY_INTERNET = 12;
    public static final int NET_CAPABILITY_NOT_METERED = 11;
    public static final int NET_CAPABILITY_VALIDATED = 16;

    NetworkCapabilities() {}

    public boolean hasTransport(int transportType) {
        return transportType == TRANSPORT_WIFI;
    }

    public boolean hasCapability(int capability) {
        return capability == NET_CAPABILITY_INTERNET || capability == NET_CAPABILITY_NOT_METERED
                || capability == NET_CAPABILITY_VALIDATED;
    }

    public int getLinkDownstreamBandwidthKbps() {
        return 100000;
    }

    public int getLinkUpstreamBandwidthKbps() {
        return 100000;
    }
}
