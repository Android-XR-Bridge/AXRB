package android.content;

import android.content.res.Resources;
import android.net.Uri;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/**
 * Content access by URI. The runner has no content providers: file: and
 * android.resource: URIs of the application are served; content: URIs are
 * reported as not found, as when no provider matches on Android.
 */
public abstract class ContentResolver {
    public static final String SCHEME_CONTENT = "content";
    public static final String SCHEME_ANDROID_RESOURCE = "android.resource";
    public static final String SCHEME_FILE = "file";
    public static final String CURSOR_ITEM_BASE_TYPE = "vnd.android.cursor.item";
    public static final String CURSOR_DIR_BASE_TYPE = "vnd.android.cursor.dir";
    public static final String ANY_CURSOR_ITEM_TYPE = "vnd.android.cursor.item/*";

    private final Context mContext;

    public ContentResolver(Context context) {
        mContext = context;
    }

    public final String getType(Uri url) {
        return null;
    }

    public final InputStream openInputStream(Uri uri) throws FileNotFoundException {
        String scheme = uri.getScheme();
        if (SCHEME_ANDROID_RESOURCE.equals(scheme)) {
            int id = resourceId(uri);
            try {
                return mContext.getResources().openRawResource(id);
            } catch (Resources.NotFoundException e) {
                throw new FileNotFoundException("Resource does not exist: " + uri);
            }
        }
        if (SCHEME_FILE.equals(scheme)) return new FileInputStream(new File(uri.getPath()));
        throw new FileNotFoundException("No content provider: " + uri);
    }

    public final OutputStream openOutputStream(Uri uri) throws FileNotFoundException {
        return openOutputStream(uri, "w");
    }

    public final OutputStream openOutputStream(Uri uri, String mode) throws FileNotFoundException {
        if (SCHEME_FILE.equals(uri.getScheme())) {
            return new FileOutputStream(new File(uri.getPath()), mode.contains("a"));
        }
        throw new FileNotFoundException("No content provider: " + uri);
    }

    private int resourceId(Uri uri) throws FileNotFoundException {
        String authority = uri.getAuthority();
        if (authority == null || authority.length() == 0) throw new FileNotFoundException("No authority: " + uri);
        List<String> path = uri.getPathSegments();
        Resources r = mContext.getResources();
        if (path.size() == 1) {
            try {
                return Integer.parseInt(path.get(0));
            } catch (NumberFormatException e) {
                throw new FileNotFoundException("Single path segment is not a resource ID: " + uri);
            }
        }
        if (path.size() == 2) {
            int id = r.getIdentifier(path.get(1), path.get(0), authority);
            if (id == 0) throw new FileNotFoundException("No resource found for: " + uri);
            return id;
        }
        throw new FileNotFoundException("More than two path segments: " + uri);
    }

    public final android.database.Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        // The OpenXR runtime broker is the only provider.
        if (OpenXrRuntimeBroker.handles(uri)) return OpenXrRuntimeBroker.query(uri);
        return null;
    }

    public final android.database.Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder, android.os.CancellationSignal cancellationSignal) {
        return query(uri, projection, selection, selectionArgs, sortOrder);
    }

    public final android.database.Cursor query(Uri uri, String[] projection, android.os.Bundle queryArgs,
            android.os.CancellationSignal cancellationSignal) {
        return query(uri, projection, null, null, null);
    }

    public final Uri insert(Uri url, ContentValues values) {
        throw new IllegalArgumentException("Unknown URL " + url);
    }

    public final int update(Uri uri, ContentValues values, String where, String[] selectionArgs) {
        throw new IllegalArgumentException("Unknown URI " + uri);
    }

    public final int delete(Uri url, String where, String[] selectionArgs) {
        throw new IllegalArgumentException("Unknown URL " + url);
    }

    public final void registerContentObserver(Uri uri, boolean notifyForDescendants,
            android.database.ContentObserver observer) {}

    public final void unregisterContentObserver(android.database.ContentObserver observer) {}

    public void notifyChange(Uri uri, android.database.ContentObserver observer) {}

    public void takePersistableUriPermission(Uri uri, int modeFlags) {
        throw new SecurityException("No persistable permission grants found for " + uri);
    }
}
