package java.util.function;

@FunctionalInterface
public interface Function<T, R> {
    R apply(T t);
}
