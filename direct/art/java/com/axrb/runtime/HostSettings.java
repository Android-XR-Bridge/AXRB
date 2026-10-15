package com.axrb.runtime;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Bundle;
import java.util.Properties;
import java.io.*;

/** Settings storage for the isolated Direct Android environment. */
public final class HostSettings extends ContentProvider {
    private final Properties values = new Properties();
    private final File storage = new File("/data/system/axrb-settings.properties");
    public boolean onCreate() {
        if (storage.isFile()) try (InputStream input = new FileInputStream(storage)) { values.load(input); }
        catch (IOException error) { throw new IllegalStateException("Cannot read settings", error); }
        return true;
    }
    private synchronized String get(String table, String name) { return values.getProperty(table + ":" + name); }
    private synchronized void put(String table, String name, String value) {
        String key = table + ":" + name;
        if (value == null) values.remove(key); else values.setProperty(key, value);
        storage.getParentFile().mkdirs();
        try (OutputStream output = new FileOutputStream(storage)) { values.store(output, "AXRB Android settings"); }
        catch (IOException error) { throw new IllegalStateException("Cannot write settings", error); }
    }
    public synchronized Bundle call(String method, String name, Bundle extras) {
        if (method.startsWith("LIST_")) {
            String table = method.substring(5) + ":";
            String prefix = extras == null ? "" : extras.getString("_prefix", "");
            java.util.HashMap<String, String> entries = new java.util.HashMap<>();
            for (String key : values.stringPropertyNames()) if (key.startsWith(table + prefix))
                entries.put(key.substring(table.length()), values.getProperty(key));
            Bundle result = new Bundle();
            result.putSerializable("value", entries);
            return result;
        }
        if (method.startsWith("GET_")) {
            Bundle result = new Bundle();
            result.putString("value", get(method.substring(4), name));
            return result;
        }
        if (method.startsWith("PUT_")) {
            put(method.substring(4), name, extras == null ? null : extras.getString("value"));
            return null;
        }
        throw new UnsupportedOperationException("Settings call " + method);
    }
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        String[] columns = projection == null ? new String[]{"name", "value"} : projection;
        MatrixCursor cursor = new MatrixCursor(columns);
        String name = uri.getPathSegments().size() > 1 ? uri.getLastPathSegment() : args != null && args.length > 0 ? args[0] : null;
        if (name != null) {
            String value = get(uri.getPathSegments().get(0), name);
            if (value != null) {
                Object[] row = new Object[columns.length];
                for (int i = 0; i < columns.length; ++i) row[i] = columns[i].equals("name") ? name : value;
                cursor.addRow(row);
            }
        }
        return cursor;
    }
    public String getType(Uri uri) { return "vnd.android.cursor.item/setting"; }
    public Uri insert(Uri uri, ContentValues values) {
        String name = values.getAsString("name");
        put(uri.getPathSegments().get(0), name, values.getAsString("value"));
        return Uri.withAppendedPath(uri, name);
    }
    public int update(Uri uri, ContentValues values, String selection, String[] args) { insert(uri, values); return 1; }
    public int delete(Uri uri, String selection, String[] args) {
        String name = uri.getPathSegments().size() > 1 ? uri.getLastPathSegment() : args[0];
        put(uri.getPathSegments().get(0), name, null);
        return 1;
    }
}
