package java.util.function;

@FunctionalInterface
public interface BiConsumer<T, U> {
    void accept(T t, U u);
}
