package android.location;

public class Location {
    private final String mProvider;
    private double mLatitude;
    private double mLongitude;
    private long mTime;

    public Location(String provider) {
        mProvider = provider;
    }

    public String getProvider() {
        return mProvider;
    }

    public double getLatitude() {
        return mLatitude;
    }

    public double getLongitude() {
        return mLongitude;
    }

    public long getTime() {
        return mTime;
    }

    public void setLatitude(double latitude) {
        mLatitude = latitude;
    }

    public void setLongitude(double longitude) {
        mLongitude = longitude;
    }

    public void setTime(long time) {
        mTime = time;
    }

    public boolean hasAccuracy() {
        return false;
    }

    public float getAccuracy() {
        return 0;
    }

    public boolean hasAltitude() {
        return false;
    }

    public double getAltitude() {
        return 0;
    }
}
