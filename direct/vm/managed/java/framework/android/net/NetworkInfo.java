package android.net;

public class NetworkInfo {
    public enum State { CONNECTING, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, UNKNOWN }

    NetworkInfo() {}

    public int getType() {
        return ConnectivityManager.TYPE_WIFI;
    }

    public String getTypeName() {
        return "WIFI";
    }

    public int getSubtype() {
        return 0;
    }

    public boolean isConnected() {
        return true;
    }

    public boolean isConnectedOrConnecting() {
        return true;
    }

    public boolean isAvailable() {
        return true;
    }

    public State getState() {
        return State.CONNECTED;
    }
}
