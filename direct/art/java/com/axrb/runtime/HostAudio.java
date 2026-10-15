package com.axrb.runtime;

import java.lang.reflect.Array;
import java.util.ArrayList;

/** The stable output route used by Direct's AAudio/OpenSL ES WASAPI backend. */
public final class HostAudio {
    private HostAudio() {}

    public static int listPorts(ArrayList<Object> ports, int[] generation) throws Exception {
        if (ports == null || generation == null || generation.length != 1) return -2;
        Object gains = Array.newInstance(Class.forName("android.media.AudioGain"), 0);
        Object port = Reflect.create("android.media.AudioDevicePort",
                // Match AAudioStream_getDeviceId in the guest output backend.
                Reflect.create("android.media.AudioHandle", 2), "AXRB default output",
                new int[]{48000}, new int[]{12}, new int[0], new int[]{2, 4}, gains,
                2, "axrb:default", new int[0], new int[0]);
        ports.clear();
        ports.add(port);
        generation[0] = 1;
        return 0;
    }
}
