package java.util;

public interface NavigableSet<E> extends SortedSet<E> {
    E lower(E e);

    E floor(E e);

    E ceiling(E e);

    E higher(E e);

    E pollFirst();

    E pollLast();

    NavigableSet<E> descendingSet();

    Iterator<E> descendingIterator();

    NavigableSet<E> subSet(E fromElement, boolean fromInclusive, E toElement, boolean toInclusive);

    NavigableSet<E> headSet(E toElement, boolean inclusive);

    NavigableSet<E> tailSet(E fromElement, boolean inclusive);
}
