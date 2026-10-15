package android.net.wifi;

import android.content.Context;

/** The host's network stands in for Wi-Fi; locks have no effect. */
public class WifiManager {
    public static final int WIFI_MODE_FULL = 1;
    public static final int WIFI_MODE_SCAN_ONLY = 2;
    public static final int WIFI_MODE_FULL_HIGH_PERF = 3;
    public static final int WIFI_MODE_FULL_LOW_LATENCY = 4;

    public WifiManager(Context context) {}

    public boolean isWifiEnabled() {
        return true;
    }

    public MulticastLock createMulticastLock(String tag) {
        return new MulticastLock(tag);
    }

    public WifiLock createWifiLock(int lockType, String tag) {
        return new WifiLock(tag);
    }

    public WifiLock createWifiLock(String tag) {
        return new WifiLock(tag);
    }

    abstract static class Lock {
        final String mTag;
        private int mCount;
        private boolean mRefCounted = true;

        Lock(String tag) {
            mTag = tag;
        }

        public void acquire() {
            synchronized (this) {
                mCount = mRefCounted ? mCount + 1 : 1;
            }
        }

        public void release() {
            synchronized (this) {
                if (mRefCounted && mCount == 0) throw new RuntimeException(getClass().getSimpleName() + " under-locked " + mTag);
                mCount = mRefCounted ? mCount - 1 : 0;
            }
        }

        public void setReferenceCounted(boolean refCounted) {
            mRefCounted = refCounted;
        }

        public boolean isHeld() {
            synchronized (this) {
                return mCount > 0;
            }
        }
    }

    public class MulticastLock extends Lock {
        MulticastLock(String tag) {
            super(tag);
        }
    }

    public class WifiLock extends Lock {
        WifiLock(String tag) {
            super(tag);
        }
    }
}
