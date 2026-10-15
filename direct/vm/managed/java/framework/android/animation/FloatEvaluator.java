package android.animation;

public class FloatEvaluator implements TypeEvaluator<Number> {
    public Float evaluate(float fraction, Number startValue, Number endValue) {
        float startFloat = startValue.floatValue();
        return startFloat + fraction * (endValue.floatValue() - startFloat);
    }
}
