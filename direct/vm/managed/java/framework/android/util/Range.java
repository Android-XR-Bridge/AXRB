package android.util;

public final class Range<T extends Comparable<? super T>> {
    private final T mLower;
    private final T mUpper;

    public Range(T lower, T upper) {
        if (lower == null || upper == null) throw new NullPointerException();
        if (lower.compareTo(upper) > 0) throw new IllegalArgumentException("lower must be less than or equal to upper");
        mLower = lower;
        mUpper = upper;
    }

    public static <T extends Comparable<? super T>> Range<T> create(T lower, T upper) {
        return new Range<T>(lower, upper);
    }

    public T getLower() {
        return mLower;
    }

    public T getUpper() {
        return mUpper;
    }

    public boolean contains(T value) {
        return value.compareTo(mLower) >= 0 && value.compareTo(mUpper) <= 0;
    }

    public T clamp(T value) {
        if (value.compareTo(mLower) < 0) return mLower;
        if (value.compareTo(mUpper) > 0) return mUpper;
        return value;
    }

    public boolean equals(Object obj) {
        if (!(obj instanceof Range)) return false;
        Range<?> other = (Range<?>) obj;
        return mLower.equals(other.mLower) && mUpper.equals(other.mUpper);
    }

    public int hashCode() {
        return mLower.hashCode() * 31 + mUpper.hashCode();
    }

    public String toString() {
        return "[" + mLower + ", " + mUpper + "]";
    }
}
