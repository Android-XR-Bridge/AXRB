package android.telephony;

public class PhoneStateListener {
    public static final int LISTEN_NONE = 0;
    public static final int LISTEN_SERVICE_STATE = 0x1;
    public static final int LISTEN_CALL_STATE = 0x20;

    public PhoneStateListener() {}

    public void onCallStateChanged(int state, String phoneNumber) {}

    public void onServiceStateChanged(Object serviceState) {}
}
