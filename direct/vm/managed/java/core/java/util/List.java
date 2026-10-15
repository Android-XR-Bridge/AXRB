package java.util;

import java.util.function.UnaryOperator;

public interface List<E> extends Collection<E> {
    E get(int index);

    E set(int index, E element);

    void add(int index, E element);

    E remove(int index);

    int indexOf(Object o);

    int lastIndexOf(Object o);

    ListIterator<E> listIterator();

    ListIterator<E> listIterator(int index);

    List<E> subList(int fromIndex, int toIndex);

    boolean addAll(int index, Collection<? extends E> c);

    default void replaceAll(UnaryOperator<E> operator) {
        Objects.requireNonNull(operator);
        ListIterator<E> li = listIterator();
        while (li.hasNext()) li.set(operator.apply(li.next()));
    }

    @SuppressWarnings("unchecked")
    default void sort(Comparator<? super E> c) {
        Object[] a = toArray();
        Arrays.sort(a, (Comparator<Object>) c);
        ListIterator<E> i = listIterator();
        for (Object e : a) {
            i.next();
            i.set((E) e);
        }
    }

    @SafeVarargs
    static <E> List<E> of(E... elements) {
        for (E e : elements) Objects.requireNonNull(e);
        return Collections.unmodifiableList(new ArrayList<E>(Arrays.asList(elements)));
    }

    static <E> List<E> copyOf(Collection<? extends E> coll) {
        for (E e : coll) Objects.requireNonNull(e);
        return Collections.unmodifiableList(new ArrayList<E>(coll));
    }
}
