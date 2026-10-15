package android.net;

public class Network {
    final int netId;

    Network(int netId) {
        this.netId = netId;
    }

    public long getNetworkHandle() {
        return ((long) netId << 32) | 0xfacade;
    }

    public boolean equals(Object obj) {
        return obj instanceof Network && ((Network) obj).netId == netId;
    }

    public int hashCode() {
        return netId * 11;
    }

    public String toString() {
        return Integer.toString(netId);
    }
}
