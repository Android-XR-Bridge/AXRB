package java.util;

public interface Set<E> extends Collection<E> {
    @SafeVarargs
    static <E> Set<E> of(E... elements) {
        HashSet<E> s = new LinkedHashSet<E>();
        for (E e : elements) {
            if (!s.add(Objects.requireNonNull(e))) throw new IllegalArgumentException("duplicate element: " + e);
        }
        return Collections.unmodifiableSet(s);
    }
}
