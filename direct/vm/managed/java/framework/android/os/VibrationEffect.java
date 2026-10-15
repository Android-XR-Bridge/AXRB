package android.os;

/** Nothing vibrates on the host; effects only need to be constructible. */
public abstract class VibrationEffect implements Parcelable {
    public static final int DEFAULT_AMPLITUDE = -1;
    public static final int EFFECT_CLICK = 0;
    public static final int EFFECT_DOUBLE_CLICK = 1;
    public static final int EFFECT_TICK = 2;
    public static final int EFFECT_HEAVY_CLICK = 5;

    VibrationEffect() {}

    public static VibrationEffect createOneShot(long milliseconds, int amplitude) {
        return new Simple();
    }

    public static VibrationEffect createWaveform(long[] timings, int repeat) {
        return new Simple();
    }

    public static VibrationEffect createWaveform(long[] timings, int[] amplitudes, int repeat) {
        return new Simple();
    }

    public static VibrationEffect createPredefined(int effectId) {
        return new Simple();
    }

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(Parcel dest, int flags) {}

    static final class Simple extends VibrationEffect {}
}
