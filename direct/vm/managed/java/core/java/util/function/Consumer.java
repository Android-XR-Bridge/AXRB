package java.util.function;

@FunctionalInterface
public interface Consumer<T> {
    void accept(T t);
}
