package android.view;

/** Common base class for input events. */
public abstract class InputEvent {
    InputEvent() {}

    public abstract int getDeviceId();

    public final InputDevice getDevice() {
        return InputDevice.getDevice(getDeviceId());
    }

    public abstract int getSource();

    public abstract void setSource(int source);

    public boolean isFromSource(int source) {
        return (getSource() & source) == source;
    }

    public abstract long getEventTime();

    public void recycle() {}
}
