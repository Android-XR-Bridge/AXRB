package android.os;

public abstract class VibratorManager {
    VibratorManager() {}

    public abstract int[] getVibratorIds();

    public abstract Vibrator getVibrator(int vibratorId);

    public abstract Vibrator getDefaultVibrator();

    public abstract void cancel();

    /** A manager with no vibrators. */
    static VibratorManager empty() {
        return new VibratorManager() {
            public int[] getVibratorIds() {
                return new int[0];
            }

            public Vibrator getVibrator(int vibratorId) {
                return new Vibrator();
            }

            public Vibrator getDefaultVibrator() {
                return new Vibrator();
            }

            public void cancel() {}
        };
    }

    public static VibratorManager getEmpty() {
        return empty();
    }
}
