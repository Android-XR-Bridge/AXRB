package android.location;

import android.os.Looper;
import java.util.ArrayList;
import java.util.List;

/** No location providers. */
public class LocationManager {
    public static final String GPS_PROVIDER = "gps";
    public static final String NETWORK_PROVIDER = "network";
    public static final String PASSIVE_PROVIDER = "passive";

    public LocationManager() {}

    public List<String> getProviders(boolean enabledOnly) {
        return new ArrayList<String>();
    }

    public List<String> getAllProviders() {
        return new ArrayList<String>();
    }

    public boolean isProviderEnabled(String provider) {
        return false;
    }

    public Location getLastKnownLocation(String provider) {
        return null;
    }

    public void requestLocationUpdates(String provider, long minTimeMs, float minDistanceM, LocationListener listener) {
        throw new IllegalArgumentException("provider \"" + provider + "\" does not exist");
    }

    public void requestLocationUpdates(String provider, long minTimeMs, float minDistanceM, LocationListener listener,
            Looper looper) {
        throw new IllegalArgumentException("provider \"" + provider + "\" does not exist");
    }

    public void removeUpdates(LocationListener listener) {}
}
