package android.animation;

/** Interpolates between two values of a type. */
public interface TypeEvaluator<T> {
    T evaluate(float fraction, T startValue, T endValue);
}
