package android.app;

import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

/**
 * Preferences kept in memory and persisted to data/shared_prefs/NAME.xml in
 * Android's format, with the same backup-file protocol: the previous file is
 * renamed to NAME.xml.bak before a write and restored if a write never
 * completed.
 *
 * Differences from Android: the file is loaded synchronously on first use,
 * apply() writes before returning (Android writes on a background thread),
 * and change listeners are held strongly (Android holds them weakly).
 */
final class SharedPreferencesImpl implements SharedPreferences {
    private static final String TAG = "SharedPreferencesImpl";
    private static final Object CONTENT = new Object();

    private final File mFile;
    private final File mBackupFile;
    private final Object mLock = new Object();
    private final Object mWritingToDiskLock = new Object();
    private HashMap<String, Object> mMap;
    private final HashMap<OnSharedPreferenceChangeListener, Object> mListeners =
            new HashMap<OnSharedPreferenceChangeListener, Object>();

    SharedPreferencesImpl(File file) {
        mFile = file;
        mBackupFile = makeBackupFile(file);
        mMap = loadFromDisk();
    }

    static File makeBackupFile(File prefsFile) {
        return new File(prefsFile.getPath() + ".bak");
    }

    private HashMap<String, Object> loadFromDisk() {
        if (mBackupFile.exists()) {
            mFile.delete();
            mBackupFile.renameTo(mFile);
        }
        if (!mFile.exists()) return new HashMap<String, Object>();
        try {
            byte[] bytes = readFully(mFile);
            return SharedPreferencesXml.read(new String(bytes, "UTF-8"));
        } catch (IOException e) {
            Log.w(TAG, "Cannot read " + mFile.getPath(), e);
            return new HashMap<String, Object>();
        }
    }

    private static byte[] readFully(File f) throws IOException {
        FileInputStream in = new FileInputStream(f);
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf, 0, buf.length)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } finally {
            in.close();
        }
    }

    // --- reads --------------------------------------------------------------------

    @Override
    public Map<String, ?> getAll() {
        synchronized (mLock) {
            return new HashMap<String, Object>(mMap);
        }
    }

    @Override
    public String getString(String key, String defValue) {
        synchronized (mLock) {
            String v = (String) mMap.get(key);
            return v != null ? v : defValue;
        }
    }

    @Override
    @SuppressWarnings("unchecked")
    public Set<String> getStringSet(String key, Set<String> defValues) {
        synchronized (mLock) {
            Set<String> v = (Set<String>) mMap.get(key);
            return v != null ? v : defValues;
        }
    }

    @Override
    public int getInt(String key, int defValue) {
        synchronized (mLock) {
            Integer v = (Integer) mMap.get(key);
            return v != null ? v.intValue() : defValue;
        }
    }

    @Override
    public long getLong(String key, long defValue) {
        synchronized (mLock) {
            Long v = (Long) mMap.get(key);
            return v != null ? v.longValue() : defValue;
        }
    }

    @Override
    public float getFloat(String key, float defValue) {
        synchronized (mLock) {
            Float v = (Float) mMap.get(key);
            return v != null ? v.floatValue() : defValue;
        }
    }

    @Override
    public boolean getBoolean(String key, boolean defValue) {
        synchronized (mLock) {
            Boolean v = (Boolean) mMap.get(key);
            return v != null ? v.booleanValue() : defValue;
        }
    }

    @Override
    public boolean contains(String key) {
        synchronized (mLock) {
            return mMap.containsKey(key);
        }
    }

    @Override
    public Editor edit() {
        return new EditorImpl();
    }

    @Override
    public void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        synchronized (mLock) {
            mListeners.put(listener, CONTENT);
        }
    }

    @Override
    public void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        synchronized (mLock) {
            mListeners.remove(listener);
        }
    }

    // --- writes -------------------------------------------------------------------

    private static final class Commit {
        boolean changesMade;
        final ArrayList<String> keysModified = new ArrayList<String>();
        OnSharedPreferenceChangeListener[] listeners;
        HashMap<String, Object> snapshot;
    }

    /** Writes `snapshot` to disk; false (leaving the backup in place) on failure. */
    private boolean writeToFile(HashMap<String, Object> snapshot) {
        synchronized (mWritingToDiskLock) {
            if (mFile.exists()) {
                if (!mBackupFile.exists()) {
                    if (!mFile.renameTo(mBackupFile)) {
                        Log.e(TAG, "Couldn't rename file " + mFile.getPath() + " to backup file " + mBackupFile.getPath());
                        return false;
                    }
                } else {
                    mFile.delete();
                }
            }
            try {
                File dir = mFile.getParentFile();
                if (dir != null && !dir.exists()) dir.mkdirs();
                byte[] data = SharedPreferencesXml.write(snapshot).getBytes("UTF-8");
                FileOutputStream out = new FileOutputStream(mFile);
                try {
                    out.write(data, 0, data.length);
                } finally {
                    out.close();
                }
                mBackupFile.delete();
                return true;
            } catch (IOException e) {
                Log.w(TAG, "writeToFile: Got exception:", e);
            }
            if (mFile.exists() && !mFile.delete()) Log.e(TAG, "Couldn't clean up partially-written file " + mFile.getPath());
            return false;
        }
    }

    private void notifyListeners(final Commit c) {
        if (c.listeners == null || c.listeners.length == 0 || c.keysModified.isEmpty()) return;
        if (Looper.myLooper() == Looper.getMainLooper()) {
            for (int i = c.keysModified.size() - 1; i >= 0; i--) {
                final String key = c.keysModified.get(i);
                for (OnSharedPreferenceChangeListener l : c.listeners) l.onSharedPreferenceChanged(this, key);
            }
        } else {
            new Handler(Looper.getMainLooper()).post(new Runnable() {
                public void run() {
                    notifyListeners(c);
                }
            });
        }
    }

    private final class EditorImpl implements Editor {
        private final Object mEditorLock = new Object();
        private final HashMap<String, Object> mModified = new HashMap<String, Object>();
        private boolean mClear;

        private Editor put(String key, Object value) {
            synchronized (mEditorLock) {
                mModified.put(key, value);
                return this;
            }
        }

        @Override
        public Editor putString(String key, String value) {
            return put(key, value);
        }

        @Override
        public Editor putStringSet(String key, Set<String> values) {
            return put(key, values == null ? null : new LinkedHashSet<String>(values));
        }

        @Override
        public Editor putInt(String key, int value) {
            return put(key, Integer.valueOf(value));
        }

        @Override
        public Editor putLong(String key, long value) {
            return put(key, Long.valueOf(value));
        }

        @Override
        public Editor putFloat(String key, float value) {
            return put(key, Float.valueOf(value));
        }

        @Override
        public Editor putBoolean(String key, boolean value) {
            return put(key, Boolean.valueOf(value));
        }

        @Override
        public Editor remove(String key) {
            // The editor itself marks a removal, as on Android.
            return put(key, this);
        }

        @Override
        public Editor clear() {
            synchronized (mEditorLock) {
                mClear = true;
                return this;
            }
        }

        private Commit commitToMemory() {
            Commit c = new Commit();
            synchronized (mLock) {
                if (!mListeners.isEmpty()) {
                    c.listeners = mListeners.keySet().toArray(new OnSharedPreferenceChangeListener[0]);
                }
                synchronized (mEditorLock) {
                    if (mClear) {
                        if (!mMap.isEmpty()) {
                            c.changesMade = true;
                            mMap.clear();
                        }
                        mClear = false;
                    }
                    for (Map.Entry<String, Object> e : mModified.entrySet()) {
                        String k = e.getKey();
                        Object v = e.getValue();
                        if (v == this || v == null) {
                            if (!mMap.containsKey(k)) continue;
                            mMap.remove(k);
                        } else {
                            if (mMap.containsKey(k)) {
                                Object existing = mMap.get(k);
                                if (existing != null && existing.equals(v)) continue;
                            }
                            mMap.put(k, v);
                        }
                        c.changesMade = true;
                        c.keysModified.add(k);
                    }
                    mModified.clear();
                }
                c.snapshot = new HashMap<String, Object>(mMap);
            }
            return c;
        }

        private boolean persist(Commit c) {
            if (!c.changesMade && mFile.exists()) return true;
            return writeToFile(c.snapshot);
        }

        @Override
        public boolean commit() {
            Commit c = commitToMemory();
            boolean ok = persist(c);
            notifyListeners(c);
            return ok;
        }

        @Override
        public void apply() {
            Commit c = commitToMemory();
            persist(c);
            notifyListeners(c);
        }
    }
}
