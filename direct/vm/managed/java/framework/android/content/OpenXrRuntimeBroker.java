package android.content;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import java.util.List;

/**
 * The OpenXR runtime broker (org.khronos.openxr.[system_]runtime_broker) as a
 * built-in provider: it names AXRB's runtime, which the runner installs in the
 * guest (system property axrb.openxr.runtime = its .so path).
 */
final class OpenXrRuntimeBroker {
    static final String SYSTEM_AUTHORITY = "org.khronos.openxr.system_runtime_broker";
    static final String INSTALLABLE_AUTHORITY = "org.khronos.openxr.runtime_broker";
    private static final String PACKAGE = "com.axrb.openxrruntime";

    private OpenXrRuntimeBroker() {}

    static boolean handles(Uri uri) {
        String authority = uri.getAuthority();
        return SYSTEM_AUTHORITY.equals(authority) || INSTALLABLE_AUTHORITY.equals(authority);
    }

    /** openxr/<major>/abi/<abi>/runtimes/active[/<package>/functions] */
    static Cursor query(Uri uri) {
        String so = System.getProperty("axrb.openxr.runtime");
        if (so == null) return null;
        List<String> p = uri.getPathSegments();
        if (p.size() < 6 || !"openxr".equals(p.get(0)) || !"abi".equals(p.get(2)) || !"runtimes".equals(p.get(4))
                || !"active".equals(p.get(5))) {
            return null;
        }
        if (!"arm64-v8a".equals(p.get(3))) return new MatrixCursor(new String[] {"package_name"});
        if (p.size() > 6) {
            // functions table: the runtime exports xrNegotiateLoaderRuntimeInterface under its own name
            return new MatrixCursor(new String[] {"function_name", "symbol_name"});
        }
        int slash = so.lastIndexOf('/');
        MatrixCursor c = new MatrixCursor(new String[] {"package_name", "native_lib_dir", "so_filename", "has_functions"});
        c.addRow(new Object[] {PACKAGE, so.substring(0, slash), so.substring(slash + 1), Integer.valueOf(0)});
        return c;
    }
}
