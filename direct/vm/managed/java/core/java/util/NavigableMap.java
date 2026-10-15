package java.util;

public interface NavigableMap<K, V> extends SortedMap<K, V> {
    Map.Entry<K, V> lowerEntry(K key);

    K lowerKey(K key);

    Map.Entry<K, V> floorEntry(K key);

    K floorKey(K key);

    Map.Entry<K, V> ceilingEntry(K key);

    K ceilingKey(K key);

    Map.Entry<K, V> higherEntry(K key);

    K higherKey(K key);

    Map.Entry<K, V> firstEntry();

    Map.Entry<K, V> lastEntry();

    Map.Entry<K, V> pollFirstEntry();

    Map.Entry<K, V> pollLastEntry();

    NavigableMap<K, V> descendingMap();

    NavigableSet<K> navigableKeySet();

    NavigableSet<K> descendingKeySet();

    NavigableMap<K, V> subMap(K fromKey, boolean fromInclusive, K toKey, boolean toInclusive);

    NavigableMap<K, V> headMap(K toKey, boolean inclusive);

    NavigableMap<K, V> tailMap(K fromKey, boolean inclusive);
}
