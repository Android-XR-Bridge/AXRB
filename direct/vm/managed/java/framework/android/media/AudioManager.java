package android.media;

import android.content.Context;

public class AudioManager {
    public static final int STREAM_VOICE_CALL = 0;
    public static final int STREAM_SYSTEM = 1;
    public static final int STREAM_RING = 2;
    public static final int STREAM_MUSIC = 3;
    public static final int STREAM_ALARM = 4;
    public static final int STREAM_NOTIFICATION = 5;
    public static final int MODE_NORMAL = 0;
    public static final int MODE_RINGTONE = 1;
    public static final int MODE_IN_CALL = 2;
    public static final int MODE_IN_COMMUNICATION = 3;
    public static final int AUDIOFOCUS_GAIN = 1;
    public static final int AUDIOFOCUS_LOSS = -1;
    public static final int AUDIOFOCUS_REQUEST_GRANTED = 1;
    public static final int RINGER_MODE_NORMAL = 2;
    public static final int ADJUST_RAISE = 1;
    public static final int ADJUST_LOWER = -1;
    public static final int ADJUST_SAME = 0;
    public static final int FLAG_SHOW_UI = 1;
    public static final int ERROR = -1;
    public static final String PROPERTY_OUTPUT_SAMPLE_RATE = "android.media.property.OUTPUT_SAMPLE_RATE";
    public static final String PROPERTY_OUTPUT_FRAMES_PER_BUFFER = "android.media.property.OUTPUT_FRAMES_PER_BUFFER";
    public static final String ACTION_HEADSET_PLUG = "android.intent.action.HEADSET_PLUG";
    public static final String ACTION_AUDIO_BECOMING_NOISY = "android.media.AUDIO_BECOMING_NOISY";
    public static final String ACTION_SCO_AUDIO_STATE_UPDATED = "android.media.ACTION_SCO_AUDIO_STATE_UPDATED";
    public static final String EXTRA_SCO_AUDIO_STATE = "android.media.extra.SCO_AUDIO_STATE";
    public static final String VOLUME_CHANGED_ACTION = "android.media.VOLUME_CHANGED_ACTION";
    public static final int SCO_AUDIO_STATE_DISCONNECTED = 0;
    public static final int SCO_AUDIO_STATE_CONNECTED = 1;

    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }

    private static final int[] sVolume = {5, 7, 7, 10, 7, 7};
    private static final int[] sMaxVolume = {5, 7, 7, 15, 7, 7};
    private int mMode = MODE_NORMAL;

    public AudioManager(Context context) {}

    public int getStreamVolume(int streamType) {
        return streamType >= 0 && streamType < sVolume.length ? sVolume[streamType] : 0;
    }

    public int getStreamMaxVolume(int streamType) {
        return streamType >= 0 && streamType < sMaxVolume.length ? sMaxVolume[streamType] : 0;
    }

    public int getStreamMinVolume(int streamType) {
        return 0;
    }

    public void setStreamVolume(int streamType, int index, int flags) {
        if (streamType >= 0 && streamType < sVolume.length) {
            sVolume[streamType] = Math.max(0, Math.min(index, sMaxVolume[streamType]));
        }
    }

    public void adjustStreamVolume(int streamType, int direction, int flags) {
        setStreamVolume(streamType, getStreamVolume(streamType) + direction, flags);
    }

    public boolean isStreamMute(int streamType) {
        return false;
    }

    public String getProperty(String key) {
        if (PROPERTY_OUTPUT_SAMPLE_RATE.equals(key)) return "48000";
        if (PROPERTY_OUTPUT_FRAMES_PER_BUFFER.equals(key)) return "240";
        return null;
    }

    public boolean isWiredHeadsetOn() {
        return false;
    }

    public boolean isBluetoothA2dpOn() {
        return false;
    }

    public boolean isBluetoothScoOn() {
        return false;
    }

    public boolean isBluetoothScoAvailableOffCall() {
        return false;
    }

    public boolean isSpeakerphoneOn() {
        return true;
    }

    public void setSpeakerphoneOn(boolean on) {}

    public boolean isMusicActive() {
        return false;
    }

    public boolean isMicrophoneMute() {
        return false;
    }

    public void setMicrophoneMute(boolean on) {}

    public void setMode(int mode) {
        mMode = mode;
    }

    public int getMode() {
        return mMode;
    }

    public int getRingerMode() {
        return RINGER_MODE_NORMAL;
    }

    public void startBluetoothSco() {}

    public void stopBluetoothSco() {}

    public void setBluetoothScoOn(boolean on) {}

    public int requestAudioFocus(OnAudioFocusChangeListener l, int streamType, int durationHint) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int abandonAudioFocus(OnAudioFocusChangeListener l) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int generateAudioSessionId() {
        return 1;
    }

    public AudioDeviceInfo[] getDevices(int flags) {
        return new AudioDeviceInfo[0];
    }
}
