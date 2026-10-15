package android.animation;

/** Maps elapsed fraction to animated fraction. */
public interface TimeInterpolator {
    float getInterpolation(float input);
}
