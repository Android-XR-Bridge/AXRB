package java.util;

public interface Queue<E> extends Collection<E> {
    boolean offer(E e);

    E remove();

    E poll();

    E element();

    E peek();
}
