package java.util;

import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;

public final class Optional<T> {
    private static final Optional<?> EMPTY = new Optional<Object>(null);
    private final T value;

    private Optional(T value) {
        this.value = value;
    }

    @SuppressWarnings("unchecked")
    public static <T> Optional<T> empty() {
        return (Optional<T>) EMPTY;
    }

    public static <T> Optional<T> of(T value) {
        return new Optional<T>(Objects.requireNonNull(value));
    }

    public static <T> Optional<T> ofNullable(T value) {
        return value == null ? Optional.<T>empty() : new Optional<T>(value);
    }

    public T get() {
        if (value == null) throw new NoSuchElementException("No value present");
        return value;
    }

    public boolean isPresent() {
        return value != null;
    }

    public boolean isEmpty() {
        return value == null;
    }

    public void ifPresent(Consumer<? super T> action) {
        if (value != null) action.accept(value);
    }

    public Optional<T> filter(Predicate<? super T> predicate) {
        return value == null || predicate.test(value) ? this : Optional.<T>empty();
    }

    public <U> Optional<U> map(Function<? super T, ? extends U> mapper) {
        return value == null ? Optional.<U>empty() : Optional.<U>ofNullable(mapper.apply(value));
    }

    @SuppressWarnings("unchecked")
    public <U> Optional<U> flatMap(Function<? super T, ? extends Optional<? extends U>> mapper) {
        return value == null ? Optional.<U>empty() : Objects.requireNonNull((Optional<U>) mapper.apply(value));
    }

    public T orElse(T other) {
        return value != null ? value : other;
    }

    public T orElseGet(Supplier<? extends T> supplier) {
        return value != null ? value : supplier.get();
    }

    public T orElseThrow() {
        return get();
    }

    public <X extends Throwable> T orElseThrow(Supplier<? extends X> exceptionSupplier) throws X {
        if (value != null) return value;
        throw exceptionSupplier.get();
    }

    public boolean equals(Object obj) {
        return obj instanceof Optional && Objects.equals(value, ((Optional<?>) obj).value);
    }

    public int hashCode() {
        return Objects.hashCode(value);
    }

    public String toString() {
        return value != null ? "Optional[" + value + "]" : "Optional.empty";
    }
}
