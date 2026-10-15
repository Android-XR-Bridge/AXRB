package android.content;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/** Column name to value map used by database and content provider APIs. */
public final class ContentValues implements Parcelable {
    private final HashMap<String, Object> mMap;

    public ContentValues() {
        mMap = new HashMap<String, Object>();
    }

    public ContentValues(int size) {
        mMap = new HashMap<String, Object>();
    }

    public ContentValues(ContentValues from) {
        mMap = new HashMap<String, Object>(from.mMap);
    }

    @Override
    public boolean equals(Object object) {
        return object instanceof ContentValues && mMap.equals(((ContentValues) object).mMap);
    }

    @Override
    public int hashCode() {
        return mMap.hashCode();
    }

    public void put(String key, String value) {
        mMap.put(key, value);
    }

    public void putAll(ContentValues other) {
        mMap.putAll(other.mMap);
    }

    public void put(String key, Byte value) {
        mMap.put(key, value);
    }

    public void put(String key, Short value) {
        mMap.put(key, value);
    }

    public void put(String key, Integer value) {
        mMap.put(key, value);
    }

    public void put(String key, Long value) {
        mMap.put(key, value);
    }

    public void put(String key, Float value) {
        mMap.put(key, value);
    }

    public void put(String key, Double value) {
        mMap.put(key, value);
    }

    public void put(String key, Boolean value) {
        mMap.put(key, value);
    }

    public void put(String key, byte[] value) {
        mMap.put(key, value);
    }

    public void putNull(String key) {
        mMap.put(key, null);
    }

    public int size() {
        return mMap.size();
    }

    public boolean isEmpty() {
        return mMap.isEmpty();
    }

    public void remove(String key) {
        mMap.remove(key);
    }

    public void clear() {
        mMap.clear();
    }

    public boolean containsKey(String key) {
        return mMap.containsKey(key);
    }

    public Object get(String key) {
        return mMap.get(key);
    }

    public String getAsString(String key) {
        Object value = mMap.get(key);
        return value != null ? value.toString() : null;
    }

    public Long getAsLong(String key) {
        Object value = mMap.get(key);
        if (value instanceof Number) return ((Number) value).longValue();
        if (value instanceof CharSequence) {
            try {
                return Long.valueOf(value.toString());
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return null;
    }

    public Integer getAsInteger(String key) {
        Object value = mMap.get(key);
        if (value instanceof Number) return ((Number) value).intValue();
        if (value instanceof CharSequence) {
            try {
                return Integer.valueOf(value.toString());
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return null;
    }

    public Short getAsShort(String key) {
        Integer v = getAsInteger(key);
        return v != null ? Short.valueOf(v.shortValue()) : null;
    }

    public Byte getAsByte(String key) {
        Integer v = getAsInteger(key);
        return v != null ? Byte.valueOf(v.byteValue()) : null;
    }

    public Double getAsDouble(String key) {
        Object value = mMap.get(key);
        if (value instanceof Number) return ((Number) value).doubleValue();
        if (value instanceof CharSequence) {
            try {
                return Double.valueOf(value.toString());
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return null;
    }

    public Float getAsFloat(String key) {
        Double v = getAsDouble(key);
        return v != null ? Float.valueOf(v.floatValue()) : null;
    }

    public Boolean getAsBoolean(String key) {
        Object value = mMap.get(key);
        if (value instanceof Boolean) return (Boolean) value;
        if (value instanceof CharSequence) {
            return Boolean.valueOf(value.toString()) || "1".equals(value.toString());
        }
        if (value instanceof Number) return ((Number) value).intValue() != 0;
        return null;
    }

    public byte[] getAsByteArray(String key) {
        Object value = mMap.get(key);
        return value instanceof byte[] ? (byte[]) value : null;
    }

    public Set<Map.Entry<String, Object>> valueSet() {
        return mMap.entrySet();
    }

    public Set<String> keySet() {
        return mMap.keySet();
    }

    public static final Parcelable.Creator<ContentValues> CREATOR = new Parcelable.Creator<ContentValues>() {
        @SuppressWarnings("unchecked")
        public ContentValues createFromParcel(Parcel in) {
            ContentValues v = new ContentValues();
            in.readMap(v.mMap, null);
            return v;
        }

        public ContentValues[] newArray(int size) {
            return new ContentValues[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
        parcel.writeMap(mMap);
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        for (String name : mMap.keySet()) {
            String value = getAsString(name);
            if (sb.length() > 0) sb.append(" ");
            sb.append(name).append("=").append(value);
        }
        return sb.toString();
    }
}
