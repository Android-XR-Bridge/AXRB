package com.axrb.runtime;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import java.util.List;

/** Khronos runtime discovery backed by AXRB's existing OpenXR runtime. */
public final class OpenXrProvider extends ContentProvider {
    public boolean onCreate() { return true; }
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        List<String> parts = uri.getPathSegments();
        MatrixCursor result = new MatrixCursor(new String[]{"package_name", "native_lib_dir", "so_filename", "has_functions"});
        if (parts.size() < 6 || !parts.get(0).equals("openxr") || !parts.get(2).equals("abi") ||
                !parts.get(4).equals("runtimes") || !parts.get(5).equals("active")) return result;
        if (parts.size() > 6) return new MatrixCursor(new String[]{"function_name", "symbol_name"});
        if (parts.get(1).equals("1") && parts.get(3).equals("arm64-v8a"))
            result.addRow(new Object[]{"com.axrb.openxrruntime", "/data/app/axrb/lib/arm64", "libopenxr_runtime.so", 0});
        return result;
    }
    public String getType(Uri uri) { return "vnd.android.cursor.dir/vnd.org.khronos.openxr.runtime"; }
    public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException("Runtime broker is read-only"); }
    public int update(Uri uri, ContentValues values, String selection, String[] args) { throw new UnsupportedOperationException("Runtime broker is read-only"); }
    public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException("Runtime broker is read-only"); }
}
