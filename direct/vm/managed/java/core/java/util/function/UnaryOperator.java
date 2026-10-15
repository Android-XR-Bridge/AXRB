package java.util.function;

@FunctionalInterface
public interface UnaryOperator<T> extends Function<T, T> {
    static <T> UnaryOperator<T> identity() {
        return new UnaryOperator<T>() {
            @Override
            public T apply(T t) {
                return t;
            }
        };
    }
}
