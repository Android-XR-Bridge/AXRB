package android.os;

public class Vibrator {
    public Vibrator() {}

    public boolean hasVibrator() {
        return false;
    }

    public boolean hasAmplitudeControl() {
        return false;
    }

    public void vibrate(long milliseconds) {}

    public void vibrate(long[] pattern, int repeat) {}

    public void vibrate(VibrationEffect vibe) {}

    public void cancel() {}
}
