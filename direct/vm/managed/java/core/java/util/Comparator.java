package java.util;

import java.util.function.Function;

@FunctionalInterface
public interface Comparator<T> {
    int compare(T o1, T o2);

    boolean equals(Object obj);

    default Comparator<T> reversed() {
        final Comparator<T> self = this;
        return new Comparator<T>() {
            public int compare(T a, T b) {
                return self.compare(b, a);
            }
        };
    }

    default Comparator<T> thenComparing(final Comparator<? super T> other) {
        Objects.requireNonNull(other);
        final Comparator<T> self = this;
        return new Comparator<T>() {
            public int compare(T a, T b) {
                int r = self.compare(a, b);
                return r != 0 ? r : other.compare(a, b);
            }
        };
    }

    static <T, U extends Comparable<? super U>> Comparator<T> comparing(final Function<? super T, ? extends U> key) {
        Objects.requireNonNull(key);
        return new Comparator<T>() {
            public int compare(T a, T b) {
                return key.apply(a).compareTo(key.apply(b));
            }
        };
    }

    @SuppressWarnings("unchecked")
    static <T extends Comparable<? super T>> Comparator<T> naturalOrder() {
        return (Comparator<T>) Collections.NATURAL_ORDER;
    }

    static <T extends Comparable<? super T>> Comparator<T> reverseOrder() {
        return Collections.reverseOrder();
    }
}
