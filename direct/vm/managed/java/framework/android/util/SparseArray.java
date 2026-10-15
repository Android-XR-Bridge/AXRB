package android.util;

import java.util.Arrays;

/** Maps int keys to objects with binary search over sorted key arrays. */
public class SparseArray<E> implements Cloneable {
    private static final Object DELETED = new Object();
    private boolean mGarbage;
    private int[] mKeys;
    private Object[] mValues;
    private int mSize;

    public SparseArray() {
        this(10);
    }

    public SparseArray(int initialCapacity) {
        if (initialCapacity < 1) initialCapacity = 1;
        mKeys = new int[initialCapacity];
        mValues = new Object[initialCapacity];
    }

    @Override
    @SuppressWarnings("unchecked")
    public SparseArray<E> clone() {
        SparseArray<E> clone = new SparseArray<E>(Math.max(1, mKeys.length));
        clone.mKeys = mKeys.clone();
        clone.mValues = mValues.clone();
        clone.mSize = mSize;
        clone.mGarbage = mGarbage;
        return clone;
    }

    static int binarySearch(int[] array, int size, int value) {
        int lo = 0;
        int hi = size - 1;
        while (lo <= hi) {
            final int mid = (lo + hi) >>> 1;
            final int midVal = array[mid];
            if (midVal < value) {
                lo = mid + 1;
            } else if (midVal > value) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return ~lo;
    }

    public boolean contains(int key) {
        return indexOfKey(key) >= 0;
    }

    public E get(int key) {
        return get(key, null);
    }

    @SuppressWarnings("unchecked")
    public E get(int key, E valueIfKeyNotFound) {
        int i = binarySearch(mKeys, mSize, key);
        if (i < 0 || mValues[i] == DELETED) return valueIfKeyNotFound;
        return (E) mValues[i];
    }

    public void delete(int key) {
        int i = binarySearch(mKeys, mSize, key);
        if (i >= 0 && mValues[i] != DELETED) {
            mValues[i] = DELETED;
            mGarbage = true;
        }
    }

    public void remove(int key) {
        delete(key);
    }

    public void removeAt(int index) {
        if (mValues[index] != DELETED) {
            mValues[index] = DELETED;
            mGarbage = true;
        }
    }

    public void removeAtRange(int index, int size) {
        final int end = Math.min(mSize, index + size);
        for (int i = index; i < end; i++) removeAt(i);
    }

    private void gc() {
        int n = mSize;
        int o = 0;
        int[] keys = mKeys;
        Object[] values = mValues;
        for (int i = 0; i < n; i++) {
            Object val = values[i];
            if (val != DELETED) {
                if (i != o) {
                    keys[o] = keys[i];
                    values[o] = val;
                    values[i] = null;
                }
                o++;
            }
        }
        mGarbage = false;
        mSize = o;
    }

    public void put(int key, E value) {
        int i = binarySearch(mKeys, mSize, key);
        if (i >= 0) {
            mValues[i] = value;
            return;
        }
        i = ~i;
        if (i < mSize && mValues[i] == DELETED) {
            mKeys[i] = key;
            mValues[i] = value;
            return;
        }
        if (mGarbage && mSize >= mKeys.length) {
            gc();
            i = ~binarySearch(mKeys, mSize, key);
        }
        if (mSize >= mKeys.length) {
            int n = Math.max(mSize * 2, 4);
            mKeys = Arrays.copyOf(mKeys, n);
            mValues = Arrays.copyOf(mValues, n);
        }
        if (mSize - i != 0) {
            System.arraycopy(mKeys, i, mKeys, i + 1, mSize - i);
            System.arraycopy(mValues, i, mValues, i + 1, mSize - i);
        }
        mKeys[i] = key;
        mValues[i] = value;
        mSize++;
    }

    public void set(int key, E value) {
        put(key, value);
    }

    public int size() {
        if (mGarbage) gc();
        return mSize;
    }

    public int keyAt(int index) {
        if (mGarbage) gc();
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return mKeys[index];
    }

    @SuppressWarnings("unchecked")
    public E valueAt(int index) {
        if (mGarbage) gc();
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return (E) mValues[index];
    }

    public void setValueAt(int index, E value) {
        if (mGarbage) gc();
        mValues[index] = value;
    }

    public int indexOfKey(int key) {
        if (mGarbage) gc();
        return binarySearch(mKeys, mSize, key);
    }

    public int indexOfValue(E value) {
        if (mGarbage) gc();
        for (int i = 0; i < mSize; i++) if (mValues[i] == value) return i;
        return -1;
    }

    public void clear() {
        for (int i = 0; i < mSize; i++) mValues[i] = null;
        mSize = 0;
        mGarbage = false;
    }

    public void append(int key, E value) {
        if (mSize != 0 && key <= mKeys[mSize - 1]) {
            put(key, value);
            return;
        }
        if (mGarbage && mSize >= mKeys.length) gc();
        if (mSize >= mKeys.length) {
            int n = Math.max(mSize * 2, 4);
            mKeys = Arrays.copyOf(mKeys, n);
            mValues = Arrays.copyOf(mValues, n);
        }
        mKeys[mSize] = key;
        mValues[mSize] = value;
        mSize++;
    }

    @Override
    public String toString() {
        if (size() <= 0) return "{}";
        StringBuilder buffer = new StringBuilder(mSize * 28);
        buffer.append('{');
        for (int i = 0; i < mSize; i++) {
            if (i > 0) buffer.append(", ");
            buffer.append(keyAt(i)).append('=');
            Object value = valueAt(i);
            buffer.append(value != this ? String.valueOf(value) : "(this Map)");
        }
        return buffer.append('}').toString();
    }
}
