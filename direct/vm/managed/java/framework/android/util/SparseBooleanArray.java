package android.util;

import java.util.Arrays;

public class SparseBooleanArray implements Cloneable {
    private int[] mKeys;
    private boolean[] mValues;
    private int mSize;

    public SparseBooleanArray() {
        this(10);
    }

    public SparseBooleanArray(int initialCapacity) {
        if (initialCapacity < 1) initialCapacity = 1;
        mKeys = new int[initialCapacity];
        mValues = new boolean[initialCapacity];
    }

    @Override
    public SparseBooleanArray clone() {
        SparseBooleanArray clone = new SparseBooleanArray(Math.max(1, mKeys.length));
        clone.mKeys = mKeys.clone();
        clone.mValues = mValues.clone();
        clone.mSize = mSize;
        return clone;
    }

    public boolean get(int key) {
        return get(key, false);
    }

    public boolean get(int key, boolean valueIfKeyNotFound) {
        int i = SparseArray.binarySearch(mKeys, mSize, key);
        return i < 0 ? valueIfKeyNotFound : mValues[i];
    }

    public void delete(int key) {
        int i = SparseArray.binarySearch(mKeys, mSize, key);
        if (i >= 0) removeAt(i);
    }

    public void removeAt(int index) {
        System.arraycopy(mKeys, index + 1, mKeys, index, mSize - (index + 1));
        System.arraycopy(mValues, index + 1, mValues, index, mSize - (index + 1));
        mSize--;
    }

    public void put(int key, boolean value) {
        int i = SparseArray.binarySearch(mKeys, mSize, key);
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

    public int keyAt(int index) {
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return mKeys[index];
    }

    public boolean valueAt(int index) {
        if (index >= mSize) throw new ArrayIndexOutOfBoundsException(index);
        return mValues[index];
    }

    public void setValueAt(int index, boolean value) {
        mValues[index] = value;
    }

    public int indexOfKey(int key) {
        return SparseArray.binarySearch(mKeys, mSize, key);
    }

    public int indexOfValue(boolean value) {
        for (int i = 0; i < mSize; i++) if (mValues[i] == value) return i;
        return -1;
    }

    public void clear() {
        mSize = 0;
    }

    public void append(int key, boolean value) {
        put(key, value);
    }

    @Override
    public String toString() {
        if (mSize <= 0) return "{}";
        StringBuilder b = new StringBuilder("{");
        for (int i = 0; i < mSize; i++) {
            if (i > 0) b.append(", ");
            b.append(mKeys[i]).append('=').append(mValues[i]);
        }
        return b.append('}').toString();
    }
}
