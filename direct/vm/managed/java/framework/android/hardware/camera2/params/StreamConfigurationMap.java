package android.hardware.camera2.params;

import android.util.Size;

public final class StreamConfigurationMap {
    StreamConfigurationMap() {}

    public Size[] getOutputSizes(int format) {
        return new Size[0];
    }

    public <T> Size[] getOutputSizes(Class<T> klass) {
        return new Size[0];
    }
}
