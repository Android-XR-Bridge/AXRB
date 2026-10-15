package android.database;

import java.util.ArrayList;

/** A cursor over rows held in memory. */
public class MatrixCursor extends AbstractCursor {
    private final String[] columnNames;
    private final ArrayList<Object[]> rows = new ArrayList<Object[]>();

    public MatrixCursor(String[] columnNames) {
        this.columnNames = columnNames;
    }

    public MatrixCursor(String[] columnNames, int initialCapacity) {
        this(columnNames);
    }

    public void addRow(Object[] columnValues) {
        if (columnValues.length != columnNames.length) {
            throw new IllegalArgumentException("columnNames.length = " + columnNames.length
                    + ", columnValues.length = " + columnValues.length);
        }
        rows.add(columnValues.clone());
    }

    public void addRow(Iterable<?> columnValues) {
        ArrayList<Object> values = new ArrayList<Object>();
        for (Object o : columnValues) values.add(o);
        addRow(values.toArray());
    }

    private Object get(int column) {
        if (column < 0 || column >= columnNames.length) {
            throw new CursorIndexOutOfBoundsException("Requested column: " + column + ", # of columns: " + columnNames.length);
        }
        checkPosition();
        return rows.get(mPos)[column];
    }

    public int getCount() {
        return rows.size();
    }

    public String[] getColumnNames() {
        return columnNames;
    }

    public String getString(int column) {
        Object value = get(column);
        return value == null ? null : value.toString();
    }

    public short getShort(int column) {
        Object value = get(column);
        if (value == null) return 0;
        if (value instanceof Number) return ((Number) value).shortValue();
        return Short.parseShort(value.toString());
    }

    public int getInt(int column) {
        Object value = get(column);
        if (value == null) return 0;
        if (value instanceof Number) return ((Number) value).intValue();
        return Integer.parseInt(value.toString());
    }

    public long getLong(int column) {
        Object value = get(column);
        if (value == null) return 0;
        if (value instanceof Number) return ((Number) value).longValue();
        return Long.parseLong(value.toString());
    }

    public float getFloat(int column) {
        Object value = get(column);
        if (value == null) return 0.0f;
        if (value instanceof Number) return ((Number) value).floatValue();
        return Float.parseFloat(value.toString());
    }

    public double getDouble(int column) {
        Object value = get(column);
        if (value == null) return 0.0d;
        if (value instanceof Number) return ((Number) value).doubleValue();
        return Double.parseDouble(value.toString());
    }

    public byte[] getBlob(int column) {
        return (byte[]) get(column);
    }

    public int getType(int column) {
        Object value = get(column);
        if (value == null) return FIELD_TYPE_NULL;
        if (value instanceof byte[]) return FIELD_TYPE_BLOB;
        if (value instanceof Float || value instanceof Double) return FIELD_TYPE_FLOAT;
        if (value instanceof Number) return FIELD_TYPE_INTEGER;
        return FIELD_TYPE_STRING;
    }

    public boolean isNull(int column) {
        return get(column) == null;
    }
}
