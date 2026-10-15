package android.util;

import java.util.Arrays;

public class LongSparseArray<E> implements Cloneable {
    private long[] mKeys;
    private Object[] mValues;
    private int mSize;

    public LongSparseArray() {
        this(10);
    }

    public LongSparseArray(int initialCapacity) {
        if (initialCapacity < 1) initialCapacity = 1;
        mKeys = new long[initialCapacity];
        mValues = new Object[initialCapacity];
    }

    @Override
    @SuppressWarnings("unchecked")
    public LongSparseArray<E> clone() {
        LongSparseArray<E> clone = new LongSparseArray<E>(Math.max(1, mKeys.length));
        clone.mKeys = mKeys.clone();
        clone.mValues = mValues.clone();
        clone.mSize = mSize;
        return clone;
    }

    private int search(long key) {
        int lo = 0;
        int hi = mSize - 1;
        while (lo <= hi) {
            final int mid = (lo + hi) >>> 1;
            final long v = mKeys[mid];
            if (v < key) lo = mid + 1;
            else if (v > key) hi = mid - 1;
            else return mid;
        }
        return ~lo;
    }

    public E get(long key) {
        return get(key, null);
    }

    @SuppressWarnings("unchecked")
    public E get(long key, E valueIfKeyNotFound) {
        int i = search(key);
        return i < 0 ? valueIfKeyNotFound : (E) mValues[i];
    }

    public void delete(long key) {
        int i = search(key);
        if (i >= 0) removeAt(i);
    }

    public void remove(long key) {
        delete(key);
    }

    public void removeAt(int index) {
        System.arraycopy(mKeys, index + 1, mKeys, index, mSize - (index + 1));
        System.arraycopy(mValues, index + 1, mValues, index, mSize - (index + 1));
        mSize--;
        mValues[mSize] = null;
    }

    public void put(long key, E value) {
        int i = search(key);
        if (i >= 0) {
            mValues[i] = value;
            return;
        }
        i = ~i;
        if (mSize >= mKeys.length) {
            int n = Math.max(mSize * 2, 4);
            mKeys = Arrays.copyOf(mKeys, n);
            mValues = Arrays.copyOf(mValues, n);
        }
        System.arraycopy(mKeys, i, mKeys, i + 1, mSize - i);
        System.arraycopy(mValues, i, mValues, i + 1, mSize - i);
        mKeys[i] = key;
        mValues[i] = value;
        mSize++;
    }

    public int size() {
        return mSize;
    }

    public long keyAt(int index) {
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return mKeys[index];
    }

    @SuppressWarnings("unchecked")
    public E valueAt(int index) {
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return (E) mValues[index];
    }

    public void setValueAt(int index, E value) {
        mValues[index] = value;
    }

    public int indexOfKey(long key) {
        return search(key);
    }

    public int indexOfValue(E value) {
        for (int i = 0; i < mSize; i++) if (mValues[i] == value) return i;
        return -1;
    }

    public void clear() {
        for (int i = 0; i < mSize; i++) mValues[i] = null;
        mSize = 0;
    }

    public void append(long key, E value) {
        put(key, value);
    }
}
