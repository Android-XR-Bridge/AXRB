package com.axrb.audioprobe;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.media.*;
import java.util.Arrays;

/** Exercise the real AOSP enumeration and initial device-callback path. */
public final class AudioDeviceProbe extends Activity {
    private boolean delivered;
    private static void require(boolean ok, String text) {
        if (!ok) throw new AssertionError(text);
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        AudioManager manager = (AudioManager)getSystemService(AUDIO_SERVICE);
        AudioDeviceInfo[] outputs = manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        require(outputs.length == 1, "one default output route");
        AudioDeviceInfo output = outputs[0];
        require(output.isSink() && !output.isSource(), "output direction");
        require(output.getId() == 2, "same route ID as the AAudio backend");
        require(Arrays.stream(output.getSampleRates()).anyMatch(v -> v == 48000), "48kHz");
        require(Arrays.stream(output.getChannelCounts()).anyMatch(v -> v == 2), "stereo");
        require(Arrays.stream(output.getEncodings()).anyMatch(v -> v == AudioFormat.ENCODING_PCM_16BIT), "PCM16");
        require(Arrays.stream(output.getEncodings()).anyMatch(v -> v == AudioFormat.ENCODING_PCM_FLOAT), "float PCM");
        require(manager.getDevices(AudioManager.GET_DEVICES_INPUTS).length == 0, "no microphone advertised");
        require(manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)[0].getId() == output.getId(), "stable route");
        require(!manager.isBluetoothA2dpOn(), "no guest Bluetooth route");
        Handler handler = new Handler(Looper.getMainLooper());
        AudioDeviceCallback callback = new AudioDeviceCallback() {
            @Override public void onAudioDevicesAdded(AudioDeviceInfo[] added) {
                require(added.length == 1 && added[0].getId() == output.getId(), "initial output callback");
                delivered = true;
                manager.unregisterAudioDeviceCallback(this);
                System.out.println("AUDIO_DEVICE_PROBE_PASS");
                System.exit(0);
            }
        };
        manager.registerAudioDeviceCallback(callback, handler);
        handler.postDelayed(() -> {
            if (!delivered) { System.err.println("AUDIO_DEVICE_CALLBACK_TIMEOUT"); System.exit(1); }
        }, 10000);
    }
}
