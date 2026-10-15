package java.util.function;

@FunctionalInterface
public interface BinaryOperator<T> extends BiFunction<T, T, T> {
    // apply(T, T) is inherited from BiFunction.
}
