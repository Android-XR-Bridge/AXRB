package android.hardware.input;

import android.os.Handler;
import android.view.InputDevice;

/** No input devices: VR input arrives through OpenXR. */
public final class InputManager {
    public interface InputDeviceListener {
        void onInputDeviceAdded(int deviceId);

        void onInputDeviceRemoved(int deviceId);

        void onInputDeviceChanged(int deviceId);
    }

    public InputManager() {}

    public InputDevice getInputDevice(int id) {
        return InputDevice.getDevice(id);
    }

    public int[] getInputDeviceIds() {
        return InputDevice.getDeviceIds();
    }

    public void registerInputDeviceListener(InputDeviceListener listener, Handler handler) {}

    public void unregisterInputDeviceListener(InputDeviceListener listener) {}
}
