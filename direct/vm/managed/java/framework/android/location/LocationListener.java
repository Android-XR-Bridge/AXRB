package android.location;

import android.os.Bundle;

public interface LocationListener {
    void onLocationChanged(Location location);

    default void onStatusChanged(String provider, int status, Bundle extras) {}

    default void onProviderEnabled(String provider) {}

    default void onProviderDisabled(String provider) {}
}
