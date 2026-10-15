package android.os;

import android.text.TextUtils;
import android.util.SparseArray;
import android.util.SparseBooleanArray;
import java.io.Serializable;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * An in-process container of flattened values. Values are stored as a
 * sequence of typed slots rather than bytes: positions count slots, and
 * marshall()/unmarshall() are not available (there is no IPC in the runner).
 * Parcelables go through their own writeToParcel and CREATOR, so the
 * semantics of flattening (copying) match Android's.
 */
public final class Parcel {
    public static final Parcelable.Creator<String> STRING_CREATOR = new Parcelable.Creator<String>() {
        public String createFromParcel(Parcel source) {
            return source.readString();
        }

        public String[] newArray(int size) {
            return new String[size];
        }
    };

    private final ArrayList<Object> mData = new ArrayList<Object>();
    private int mPos;

    private Parcel() {}

    public static Parcel obtain() {
        return new Parcel();
    }

    public final void recycle() {
        mData.clear();
        mPos = 0;
    }

    public final int dataSize() {
        return mData.size();
    }

    public final int dataAvail() {
        return mData.size() - mPos;
    }

    public final int dataPosition() {
        return mPos;
    }

    public final int dataCapacity() {
        return mData.size();
    }

    public final void setDataSize(int size) {
        while (mData.size() > size) mData.remove(mData.size() - 1);
        if (mPos > size) mPos = size;
    }

    public final void setDataPosition(int pos) {
        if (pos < 0 || pos > mData.size()) throw new IllegalArgumentException("position " + pos);
        mPos = pos;
    }

    public final void setDataCapacity(int size) {}

    public final byte[] marshall() {
        throw new UnsupportedOperationException("Parcel.marshall: parcels are not byte streams in this runtime");
    }

    public final void unmarshall(byte[] data, int offset, int length) {
        throw new UnsupportedOperationException("Parcel.unmarshall: parcels are not byte streams in this runtime");
    }

    public final void appendFrom(Parcel parcel, int offset, int length) {
        for (int i = offset; i < offset + length && i < parcel.mData.size(); i++) put(parcel.mData.get(i));
    }

    public final boolean hasFileDescriptors() {
        return false;
    }

    public final void writeInterfaceToken(String interfaceName) {
        writeString(interfaceName);
    }

    public final void enforceInterface(String interfaceName) {
        String token = readString();
        if (!TextUtils.equals(token, interfaceName)) {
            throw new SecurityException("Binder invocation to an incorrect interface");
        }
    }

    private void put(Object o) {
        if (mPos < mData.size()) {
            mData.set(mPos, o);
        } else {
            mData.add(o);
        }
        mPos++;
    }

    private Object take() {
        if (mPos >= mData.size()) return null;
        return mData.get(mPos++);
    }

    // --- primitives --------------------------------------------------------------

    public final void writeInt(int val) {
        put(Integer.valueOf(val));
    }

    public final void writeLong(long val) {
        put(Long.valueOf(val));
    }

    public final void writeFloat(float val) {
        put(Float.valueOf(val));
    }

    public final void writeDouble(double val) {
        put(Double.valueOf(val));
    }

    public final void writeByte(byte val) {
        writeInt(val);
    }

    public final void writeBoolean(boolean val) {
        writeInt(val ? 1 : 0);
    }

    public final void writeString(String val) {
        put(val);
    }

    public final void writeString8(String val) {
        writeString(val);
    }

    public final void writeString16(String val) {
        writeString(val);
    }

    public final void writeCharSequence(CharSequence val) {
        put(val == null ? null : val.toString());
    }

    public final int readInt() {
        Object o = take();
        return o instanceof Number ? ((Number) o).intValue() : 0;
    }

    public final long readLong() {
        Object o = take();
        return o instanceof Number ? ((Number) o).longValue() : 0L;
    }

    public final float readFloat() {
        Object o = take();
        return o instanceof Number ? ((Number) o).floatValue() : 0f;
    }

    public final double readDouble() {
        Object o = take();
        return o instanceof Number ? ((Number) o).doubleValue() : 0d;
    }

    public final byte readByte() {
        return (byte) readInt();
    }

    public final boolean readBoolean() {
        return readInt() != 0;
    }

    public final String readString() {
        Object o = take();
        return o == null ? null : o.toString();
    }

    public final String readString8() {
        return readString();
    }

    public final String readString16() {
        return readString();
    }

    public final CharSequence readCharSequence() {
        return readString();
    }

    // --- arrays --------------------------------------------------------------------

    public final void writeByteArray(byte[] b) {
        put(b == null ? null : b.clone());
    }

    public final void writeByteArray(byte[] b, int offset, int len) {
        if (b == null) {
            put(null);
            return;
        }
        byte[] copy = new byte[len];
        System.arraycopy(b, offset, copy, 0, len);
        put(copy);
    }

    public final void writeBlob(byte[] b) {
        writeByteArray(b);
    }

    public final byte[] createByteArray() {
        Object o = take();
        return o instanceof byte[] ? ((byte[]) o).clone() : null;
    }

    public final void readByteArray(byte[] val) {
        byte[] b = createByteArray();
        if (b == null || b.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(b, 0, val, 0, b.length);
    }

    public final byte[] readBlob() {
        return createByteArray();
    }

    public final void writeIntArray(int[] val) {
        put(val == null ? null : val.clone());
    }

    public final int[] createIntArray() {
        Object o = take();
        return o instanceof int[] ? ((int[]) o).clone() : null;
    }

    public final void readIntArray(int[] val) {
        int[] a = createIntArray();
        if (a == null || a.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(a, 0, val, 0, a.length);
    }

    public final void writeLongArray(long[] val) {
        put(val == null ? null : val.clone());
    }

    public final long[] createLongArray() {
        Object o = take();
        return o instanceof long[] ? ((long[]) o).clone() : null;
    }

    public final void readLongArray(long[] val) {
        long[] a = createLongArray();
        if (a == null || a.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(a, 0, val, 0, a.length);
    }

    public final void writeFloatArray(float[] val) {
        put(val == null ? null : val.clone());
    }

    public final float[] createFloatArray() {
        Object o = take();
        return o instanceof float[] ? ((float[]) o).clone() : null;
    }

    public final void readFloatArray(float[] val) {
        float[] a = createFloatArray();
        if (a == null || a.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(a, 0, val, 0, a.length);
    }

    public final void writeDoubleArray(double[] val) {
        put(val == null ? null : val.clone());
    }

    public final double[] createDoubleArray() {
        Object o = take();
        return o instanceof double[] ? ((double[]) o).clone() : null;
    }

    public final void writeBooleanArray(boolean[] val) {
        put(val == null ? null : val.clone());
    }

    public final boolean[] createBooleanArray() {
        Object o = take();
        return o instanceof boolean[] ? ((boolean[]) o).clone() : null;
    }

    public final void readBooleanArray(boolean[] val) {
        boolean[] a = createBooleanArray();
        if (a == null || a.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(a, 0, val, 0, a.length);
    }

    public final void writeCharArray(char[] val) {
        put(val == null ? null : val.clone());
    }

    public final char[] createCharArray() {
        Object o = take();
        return o instanceof char[] ? ((char[]) o).clone() : null;
    }

    public final void writeStringArray(String[] val) {
        put(val == null ? null : val.clone());
    }

    public final String[] createStringArray() {
        Object o = take();
        return o instanceof String[] ? ((String[]) o).clone() : null;
    }

    public final void readStringArray(String[] val) {
        String[] a = createStringArray();
        if (a == null || a.length != val.length) throw new RuntimeException("bad array lengths");
        System.arraycopy(a, 0, val, 0, a.length);
    }

    public final void writeStringList(List<String> val) {
        put(val == null ? null : new ArrayList<String>(val));
    }

    @SuppressWarnings("unchecked")
    public final ArrayList<String> createStringArrayList() {
        Object o = take();
        return o instanceof List ? new ArrayList<String>((List<String>) o) : null;
    }

    public final void readStringList(List<String> list) {
        ArrayList<String> l = createStringArrayList();
        list.clear();
        if (l != null) list.addAll(l);
    }

    // --- parcelables -----------------------------------------------------------------

    public final void writeParcelable(Parcelable p, int parcelableFlags) {
        if (p == null) {
            put(null);
            return;
        }
        put(p.getClass().getName());
        p.writeToParcel(this, parcelableFlags);
    }

    public final <T extends Parcelable> T readParcelable(ClassLoader loader) {
        String name = readString();
        if (name == null) return null;
        Parcelable.Creator<?> creator = readParcelableCreator(name, loader);
        Object value;
        if (creator instanceof Parcelable.ClassLoaderCreator) {
            value = ((Parcelable.ClassLoaderCreator<?>) creator).createFromParcel(this, loader);
        } else {
            value = creator.createFromParcel(this);
        }
        @SuppressWarnings("unchecked")
        T result = (T) value;
        return result;
    }

    public final <T> T readParcelable(ClassLoader loader, Class<T> clazz) {
        Object p = readParcelable(loader);
        return clazz.cast(p);
    }

    private static final HashMap<String, Parcelable.Creator<?>> sCreators = new HashMap<String, Parcelable.Creator<?>>();

    public final Parcelable.Creator<?> readParcelableCreator(String name, ClassLoader loader) {
        synchronized (sCreators) {
            Parcelable.Creator<?> c = sCreators.get(name);
            if (c != null) return c;
        }
        try {
            ClassLoader l = loader != null ? loader : Parcel.class.getClassLoader();
            Class<?> cls = Class.forName(name, false, l);
            Field f = cls.getField("CREATOR");
            Object creator = f.get(null);
            if (!(creator instanceof Parcelable.Creator)) {
                throw new BadParcelableException("Parcelable protocol requires a Parcelable.Creator object called "
                        + "CREATOR on class " + name);
            }
            synchronized (sCreators) {
                sCreators.put(name, (Parcelable.Creator<?>) creator);
            }
            return (Parcelable.Creator<?>) creator;
        } catch (ClassNotFoundException e) {
            throw new BadParcelableException("ClassNotFoundException when unmarshalling: " + name);
        } catch (NoSuchFieldException e) {
            throw new BadParcelableException("Parcelable protocol requires a Parcelable.Creator object called "
                    + "CREATOR on class " + name);
        } catch (IllegalAccessException e) {
            throw new BadParcelableException("IllegalAccessException when unmarshalling: " + name);
        }
    }

    public final void writeParcelableArray(Parcelable[] value, int parcelableFlags) {
        if (value == null) {
            writeInt(-1);
            return;
        }
        writeInt(value.length);
        for (Parcelable p : value) writeParcelable(p, parcelableFlags);
    }

    public final Parcelable[] readParcelableArray(ClassLoader loader) {
        int n = readInt();
        if (n < 0) return null;
        Parcelable[] out = new Parcelable[n];
        for (int i = 0; i < n; i++) out[i] = readParcelable(loader);
        return out;
    }

    public final <T extends Parcelable> void writeTypedList(List<T> val) {
        writeTypedList(val, 0);
    }

    public final <T extends Parcelable> void writeTypedList(List<T> val, int flags) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        writeInt(val.size());
        for (T item : val) writeTypedObject(item, flags);
    }

    public final <T> ArrayList<T> createTypedArrayList(Parcelable.Creator<T> c) {
        int n = readInt();
        if (n < 0) return null;
        ArrayList<T> l = new ArrayList<T>(n);
        for (int i = 0; i < n; i++) l.add(readTypedObject(c));
        return l;
    }

    public final <T> void readTypedList(List<T> list, Parcelable.Creator<T> c) {
        ArrayList<T> l = createTypedArrayList(c);
        list.clear();
        if (l != null) list.addAll(l);
    }

    public final <T extends Parcelable> void writeTypedArray(T[] val, int parcelableFlags) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        writeInt(val.length);
        for (T item : val) writeTypedObject(item, parcelableFlags);
    }

    public final <T> T[] createTypedArray(Parcelable.Creator<T> c) {
        int n = readInt();
        if (n < 0) return null;
        T[] l = c.newArray(n);
        for (int i = 0; i < n; i++) l[i] = readTypedObject(c);
        return l;
    }

    public final <T extends Parcelable> void writeTypedObject(T val, int parcelableFlags) {
        if (val != null) {
            writeInt(1);
            val.writeToParcel(this, parcelableFlags);
        } else {
            writeInt(0);
        }
    }

    public final <T> T readTypedObject(Parcelable.Creator<T> c) {
        return readInt() != 0 ? c.createFromParcel(this) : null;
    }

    // --- bundles and generic values ---------------------------------------------------

    public final void writeBundle(Bundle val) {
        if (val == null) {
            put(null);
            return;
        }
        put("Bundle");
        val.writeToParcel(this, 0);
    }

    public final Bundle readBundle() {
        return readBundle(null);
    }

    public final Bundle readBundle(ClassLoader loader) {
        Object tag = take();
        if (tag == null) return null;
        Bundle b = new Bundle(loader);
        b.readFromParcel(this);
        return b;
    }

    public final void writeSerializable(Serializable s) {
        // No object streams: the object is shared, not copied.
        put(new SerializableRef(s));
    }

    public final Serializable readSerializable() {
        Object o = take();
        return o instanceof SerializableRef ? ((SerializableRef) o).value : null;
    }

    private static final class SerializableRef {
        final Serializable value;

        SerializableRef(Serializable value) {
            this.value = value;
        }
    }

    public final void writeStrongBinder(IBinder val) {
        put(new BinderRef(val));
    }

    public final IBinder readStrongBinder() {
        Object o = take();
        return o instanceof BinderRef ? ((BinderRef) o).value : null;
    }

    private static final class BinderRef {
        final IBinder value;

        BinderRef(IBinder value) {
            this.value = value;
        }
    }

    private static final int VAL_NULL = -1;
    private static final int VAL_STRING = 0;
    private static final int VAL_INTEGER = 1;
    private static final int VAL_MAP = 2;
    private static final int VAL_BUNDLE = 3;
    private static final int VAL_PARCELABLE = 4;
    private static final int VAL_SHORT = 5;
    private static final int VAL_LONG = 6;
    private static final int VAL_FLOAT = 7;
    private static final int VAL_DOUBLE = 8;
    private static final int VAL_BOOLEAN = 9;
    private static final int VAL_CHARSEQUENCE = 10;
    private static final int VAL_LIST = 11;
    private static final int VAL_SPARSEARRAY = 12;
    private static final int VAL_BYTEARRAY = 13;
    private static final int VAL_STRINGARRAY = 14;
    private static final int VAL_IBINDER = 15;
    private static final int VAL_PARCELABLEARRAY = 16;
    private static final int VAL_OBJECTARRAY = 17;
    private static final int VAL_INTARRAY = 18;
    private static final int VAL_LONGARRAY = 19;
    private static final int VAL_BYTE = 20;
    private static final int VAL_SERIALIZABLE = 21;
    private static final int VAL_SPARSEBOOLEANARRAY = 22;
    private static final int VAL_BOOLEANARRAY = 23;
    private static final int VAL_CHARSEQUENCEARRAY = 24;
    private static final int VAL_FLOATARRAY = 25;
    private static final int VAL_DOUBLEARRAY = 26;
    private static final int VAL_CHAR = 27;
    private static final int VAL_CHARARRAY = 28;

    public final void writeValue(Object v) {
        if (v == null) {
            writeInt(VAL_NULL);
        } else if (v instanceof String) {
            writeInt(VAL_STRING);
            writeString((String) v);
        } else if (v instanceof Integer) {
            writeInt(VAL_INTEGER);
            writeInt((Integer) v);
        } else if (v instanceof Map) {
            writeInt(VAL_MAP);
            writeMap((Map<?, ?>) v);
        } else if (v instanceof Bundle) {
            writeInt(VAL_BUNDLE);
            writeBundle((Bundle) v);
        } else if (v instanceof Parcelable) {
            writeInt(VAL_PARCELABLE);
            writeParcelable((Parcelable) v, 0);
        } else if (v instanceof Short) {
            writeInt(VAL_SHORT);
            writeInt(((Short) v).intValue());
        } else if (v instanceof Long) {
            writeInt(VAL_LONG);
            writeLong((Long) v);
        } else if (v instanceof Float) {
            writeInt(VAL_FLOAT);
            writeFloat((Float) v);
        } else if (v instanceof Double) {
            writeInt(VAL_DOUBLE);
            writeDouble((Double) v);
        } else if (v instanceof Boolean) {
            writeInt(VAL_BOOLEAN);
            writeInt((Boolean) v ? 1 : 0);
        } else if (v instanceof Byte) {
            writeInt(VAL_BYTE);
            writeInt((Byte) v);
        } else if (v instanceof Character) {
            writeInt(VAL_CHAR);
            writeInt((Character) v);
        } else if (v instanceof CharSequence) {
            writeInt(VAL_CHARSEQUENCE);
            writeCharSequence((CharSequence) v);
        } else if (v instanceof List) {
            writeInt(VAL_LIST);
            writeList((List<?>) v);
        } else if (v instanceof SparseArray) {
            writeInt(VAL_SPARSEARRAY);
            writeSparseArray((SparseArray<?>) v);
        } else if (v instanceof SparseBooleanArray) {
            writeInt(VAL_SPARSEBOOLEANARRAY);
            writeSparseBooleanArray((SparseBooleanArray) v);
        } else if (v instanceof boolean[]) {
            writeInt(VAL_BOOLEANARRAY);
            writeBooleanArray((boolean[]) v);
        } else if (v instanceof byte[]) {
            writeInt(VAL_BYTEARRAY);
            writeByteArray((byte[]) v);
        } else if (v instanceof String[]) {
            writeInt(VAL_STRINGARRAY);
            writeStringArray((String[]) v);
        } else if (v instanceof CharSequence[]) {
            writeInt(VAL_CHARSEQUENCEARRAY);
            CharSequence[] a = (CharSequence[]) v;
            writeInt(a.length);
            for (CharSequence c : a) writeCharSequence(c);
        } else if (v instanceof IBinder) {
            writeInt(VAL_IBINDER);
            writeStrongBinder((IBinder) v);
        } else if (v instanceof Parcelable[]) {
            writeInt(VAL_PARCELABLEARRAY);
            writeParcelableArray((Parcelable[]) v, 0);
        } else if (v instanceof int[]) {
            writeInt(VAL_INTARRAY);
            writeIntArray((int[]) v);
        } else if (v instanceof long[]) {
            writeInt(VAL_LONGARRAY);
            writeLongArray((long[]) v);
        } else if (v instanceof float[]) {
            writeInt(VAL_FLOATARRAY);
            writeFloatArray((float[]) v);
        } else if (v instanceof double[]) {
            writeInt(VAL_DOUBLEARRAY);
            writeDoubleArray((double[]) v);
        } else if (v instanceof char[]) {
            writeInt(VAL_CHARARRAY);
            writeCharArray((char[]) v);
        } else if (v instanceof Object[]) {
            writeInt(VAL_OBJECTARRAY);
            Object[] a = (Object[]) v;
            writeInt(a.length);
            for (Object o : a) writeValue(o);
        } else if (v instanceof Serializable) {
            writeInt(VAL_SERIALIZABLE);
            writeSerializable((Serializable) v);
        } else {
            throw new RuntimeException("Parcel: unable to marshal value " + v);
        }
    }

    public final Object readValue(ClassLoader loader) {
        int type = readInt();
        switch (type) {
            case VAL_NULL: return null;
            case VAL_STRING: return readString();
            case VAL_INTEGER: return readInt();
            case VAL_MAP: return readHashMap(loader);
            case VAL_BUNDLE: return readBundle(loader);
            case VAL_PARCELABLE: return readParcelable(loader);
            case VAL_SHORT: return (short) readInt();
            case VAL_LONG: return readLong();
            case VAL_FLOAT: return readFloat();
            case VAL_DOUBLE: return readDouble();
            case VAL_BOOLEAN: return readInt() == 1;
            case VAL_BYTE: return readByte();
            case VAL_CHAR: return (char) readInt();
            case VAL_CHARSEQUENCE: return readCharSequence();
            case VAL_LIST: return readArrayList(loader);
            case VAL_SPARSEARRAY: return readSparseArray(loader);
            case VAL_SPARSEBOOLEANARRAY: return readSparseBooleanArray();
            case VAL_BOOLEANARRAY: return createBooleanArray();
            case VAL_BYTEARRAY: return createByteArray();
            case VAL_STRINGARRAY: return createStringArray();
            case VAL_CHARSEQUENCEARRAY: {
                int n = readInt();
                CharSequence[] a = new CharSequence[n];
                for (int i = 0; i < n; i++) a[i] = readCharSequence();
                return a;
            }
            case VAL_IBINDER: return readStrongBinder();
            case VAL_PARCELABLEARRAY: return readParcelableArray(loader);
            case VAL_INTARRAY: return createIntArray();
            case VAL_LONGARRAY: return createLongArray();
            case VAL_FLOATARRAY: return createFloatArray();
            case VAL_DOUBLEARRAY: return createDoubleArray();
            case VAL_CHARARRAY: return createCharArray();
            case VAL_OBJECTARRAY: {
                int n = readInt();
                Object[] a = new Object[n];
                for (int i = 0; i < n; i++) a[i] = readValue(loader);
                return a;
            }
            case VAL_SERIALIZABLE: return readSerializable();
            default:
                throw new BadParcelableException("Parcel: unmarshalling unknown type code " + type);
        }
    }

    public final void writeMap(Map<?, ?> val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        Set<? extends Map.Entry<?, ?>> entries = val.entrySet();
        writeInt(entries.size());
        for (Map.Entry<?, ?> e : entries) {
            writeValue(e.getKey());
            writeValue(e.getValue());
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public final void readMap(Map outVal, ClassLoader loader) {
        int n = readInt();
        for (int i = 0; i < n; i++) {
            Object key = readValue(loader);
            Object value = readValue(loader);
            outVal.put(key, value);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public final HashMap readHashMap(ClassLoader loader) {
        int n = readInt();
        if (n < 0) return null;
        HashMap m = new HashMap();
        for (int i = 0; i < n; i++) {
            Object key = readValue(loader);
            Object value = readValue(loader);
            m.put(key, value);
        }
        return m;
    }

    public final void writeList(List<?> val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        writeInt(val.size());
        for (Object o : val) writeValue(o);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public final void readList(List outVal, ClassLoader loader) {
        int n = readInt();
        for (int i = 0; i < n; i++) outVal.add(readValue(loader));
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public final ArrayList readArrayList(ClassLoader loader) {
        int n = readInt();
        if (n < 0) return null;
        ArrayList l = new ArrayList(n);
        for (int i = 0; i < n; i++) l.add(readValue(loader));
        return l;
    }

    public final void writeSparseArray(SparseArray<?> val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        int n = val.size();
        writeInt(n);
        for (int i = 0; i < n; i++) {
            writeInt(val.keyAt(i));
            writeValue(val.valueAt(i));
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public final SparseArray readSparseArray(ClassLoader loader) {
        int n = readInt();
        if (n < 0) return null;
        SparseArray sa = new SparseArray(n);
        for (int i = 0; i < n; i++) {
            int key = readInt();
            sa.append(key, readValue(loader));
        }
        return sa;
    }

    public final void writeSparseBooleanArray(SparseBooleanArray val) {
        if (val == null) {
            writeInt(-1);
            return;
        }
        int n = val.size();
        writeInt(n);
        for (int i = 0; i < n; i++) {
            writeInt(val.keyAt(i));
            writeInt(val.valueAt(i) ? 1 : 0);
        }
    }

    public final SparseBooleanArray readSparseBooleanArray() {
        int n = readInt();
        if (n < 0) return null;
        SparseBooleanArray sa = new SparseBooleanArray(n);
        for (int i = 0; i < n; i++) {
            int key = readInt();
            sa.append(key, readInt() != 0);
        }
        return sa;
    }

    public final void writeException(Exception e) {
        writeInt(-1);
        writeString(e.getMessage());
    }

    public final void writeNoException() {
        writeInt(0);
    }

    public final void readException() {
        int code = readInt();
        if (code != 0) throw new RuntimeException(readString());
    }

    @Override
    public String toString() {
        return "Parcel{" + mData.size() + " values, position " + mPos + "}";
    }

}
