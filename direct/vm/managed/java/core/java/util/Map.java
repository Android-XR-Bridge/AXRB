package java.util;

import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

public interface Map<K, V> {
    int size();

    boolean isEmpty();

    boolean containsKey(Object key);

    boolean containsValue(Object value);

    V get(Object key);

    V put(K key, V value);

    V remove(Object key);

    void putAll(Map<? extends K, ? extends V> m);

    void clear();

    Set<K> keySet();

    Collection<V> values();

    Set<Map.Entry<K, V>> entrySet();

    boolean equals(Object o);

    int hashCode();

    interface Entry<K, V> {
        K getKey();

        V getValue();

        V setValue(V value);

        boolean equals(Object o);

        int hashCode();
    }

    default V getOrDefault(Object key, V defaultValue) {
        V v = get(key);
        return (v != null || containsKey(key)) ? v : defaultValue;
    }

    default void forEach(BiConsumer<? super K, ? super V> action) {
        Objects.requireNonNull(action);
        for (Map.Entry<K, V> entry : entrySet()) action.accept(entry.getKey(), entry.getValue());
    }

    default V putIfAbsent(K key, V value) {
        V v = get(key);
        if (v == null) v = put(key, value);
        return v;
    }

    default boolean remove(Object key, Object value) {
        Object cur = get(key);
        if (!Objects.equals(cur, value) || (cur == null && !containsKey(key))) return false;
        remove(key);
        return true;
    }

    default V replace(K key, V value) {
        V cur = get(key);
        if (cur != null || containsKey(key)) cur = put(key, value);
        return cur;
    }

    default V computeIfAbsent(K key, Function<? super K, ? extends V> mappingFunction) {
        Objects.requireNonNull(mappingFunction);
        V v = get(key);
        if (v == null) {
            V n = mappingFunction.apply(key);
            if (n != null) {
                put(key, n);
                return n;
            }
        }
        return v;
    }

    default V computeIfPresent(K key, BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        Objects.requireNonNull(remappingFunction);
        V old = get(key);
        if (old != null) {
            V n = remappingFunction.apply(key, old);
            if (n != null) {
                put(key, n);
                return n;
            }
            remove(key);
        }
        return null;
    }

    default V compute(K key, BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        Objects.requireNonNull(remappingFunction);
        V old = get(key);
        V n = remappingFunction.apply(key, old);
        if (n == null) {
            if (old != null || containsKey(key)) remove(key);
            return null;
        }
        put(key, n);
        return n;
    }

    default V merge(K key, V value, BiFunction<? super V, ? super V, ? extends V> remappingFunction) {
        Objects.requireNonNull(remappingFunction);
        Objects.requireNonNull(value);
        V old = get(key);
        V n = old == null ? value : remappingFunction.apply(old, value);
        if (n == null) {
            remove(key);
        } else {
            put(key, n);
        }
        return n;
    }

    static <K, V> Map<K, V> of() {
        return Collections.emptyMap();
    }

    static <K, V> Map<K, V> of(K k1, V v1) {
        HashMap<K, V> m = new HashMap<K, V>();
        m.put(Objects.requireNonNull(k1), Objects.requireNonNull(v1));
        return Collections.unmodifiableMap(m);
    }

    static <K, V> Map.Entry<K, V> entry(K k, V v) {
        return new AbstractMap.SimpleImmutableEntry<K, V>(Objects.requireNonNull(k), Objects.requireNonNull(v));
    }
}
