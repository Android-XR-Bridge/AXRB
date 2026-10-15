package android.telephony;

import android.content.Context;

/** No telephony hardware. */
public class TelephonyManager {
    public static final int CALL_STATE_IDLE = 0;
    public static final int PHONE_TYPE_NONE = 0;
    public static final int NETWORK_TYPE_UNKNOWN = 0;
    public static final int SIM_STATE_ABSENT = 1;
    public static final int DATA_DISCONNECTED = 0;

    public TelephonyManager(Context context) {}

    public void listen(PhoneStateListener listener, int events) {}

    public int getCallState() {
        return CALL_STATE_IDLE;
    }

    public int getPhoneType() {
        return PHONE_TYPE_NONE;
    }

    public int getNetworkType() {
        return NETWORK_TYPE_UNKNOWN;
    }

    public int getDataNetworkType() {
        return NETWORK_TYPE_UNKNOWN;
    }

    public int getSimState() {
        return SIM_STATE_ABSENT;
    }

    public String getNetworkOperatorName() {
        return "";
    }

    public String getNetworkOperator() {
        return "";
    }

    public String getSimOperator() {
        return "";
    }

    public String getSimOperatorName() {
        return "";
    }

    public String getNetworkCountryIso() {
        return "";
    }

    public String getSimCountryIso() {
        return "";
    }

    public int getDataState() {
        return DATA_DISCONNECTED;
    }

    public boolean isNetworkRoaming() {
        return false;
    }

    public boolean hasCarrierPrivileges() {
        return false;
    }
}
