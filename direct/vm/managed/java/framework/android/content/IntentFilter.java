package android.content;

import android.net.Uri;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.PatternMatcher;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Set;

/** Intent matching by action, category and data, with AOSP's match results. */
public class IntentFilter implements Parcelable {
    public static final int SYSTEM_HIGH_PRIORITY = 1000;
    public static final int SYSTEM_LOW_PRIORITY = -1000;

    public static final int MATCH_CATEGORY_MASK = 0xfff0000;
    public static final int MATCH_ADJUSTMENT_MASK = 0x000ffff;
    public static final int MATCH_ADJUSTMENT_NORMAL = 0x8000;
    public static final int MATCH_CATEGORY_EMPTY = 0x0100000;
    public static final int MATCH_CATEGORY_SCHEME = 0x0200000;
    public static final int MATCH_CATEGORY_HOST = 0x0300000;
    public static final int MATCH_CATEGORY_PORT = 0x0400000;
    public static final int MATCH_CATEGORY_PATH = 0x0500000;
    public static final int MATCH_CATEGORY_SCHEME_SPECIFIC_PART = 0x0580000;
    public static final int MATCH_CATEGORY_TYPE = 0x0600000;
    public static final int NO_MATCH_TYPE = -1;
    public static final int NO_MATCH_DATA = -2;
    public static final int NO_MATCH_ACTION = -3;
    public static final int NO_MATCH_CATEGORY = -4;

    public static class MalformedMimeTypeException extends Exception {
        public MalformedMimeTypeException() {}

        public MalformedMimeTypeException(String name) {
            super(name);
        }
    }

    public static final class AuthorityEntry {
        private final String mHost;
        private final boolean mWild;
        private final int mPort;

        public AuthorityEntry(String host, String port) {
            mWild = host.length() > 0 && host.charAt(0) == '*';
            mHost = mWild ? host.substring(1) : host;
            mPort = port != null ? Integer.parseInt(port) : -1;
        }

        public String getHost() {
            return mWild ? "*" + mHost : mHost;
        }

        public int getPort() {
            return mPort;
        }

        public int match(Uri data) {
            String host = data.getHost();
            if (host == null) return NO_MATCH_DATA;
            if (mWild) {
                if (host.length() < mHost.length()) return NO_MATCH_DATA;
                host = host.substring(host.length() - mHost.length());
            }
            if (!host.equalsIgnoreCase(mHost)) return NO_MATCH_DATA;
            if (mPort >= 0) {
                if (mPort != data.getPort()) return NO_MATCH_DATA;
                return MATCH_CATEGORY_PORT;
            }
            return MATCH_CATEGORY_HOST;
        }
    }

    private int mPriority;
    private int mOrder;
    private final ArrayList<String> mActions = new ArrayList<String>();
    private ArrayList<String> mCategories;
    private ArrayList<String> mDataSchemes;
    private ArrayList<PatternMatcher> mDataSchemeSpecificParts;
    private ArrayList<AuthorityEntry> mDataAuthorities;
    private ArrayList<PatternMatcher> mDataPaths;
    private ArrayList<String> mDataTypes;
    private boolean mHasPartialTypes;

    public IntentFilter() {}

    public IntentFilter(String action) {
        addAction(action);
    }

    public IntentFilter(String action, String dataType) throws MalformedMimeTypeException {
        addAction(action);
        addDataType(dataType);
    }

    public IntentFilter(IntentFilter o) {
        mPriority = o.mPriority;
        mOrder = o.mOrder;
        mActions.addAll(o.mActions);
        if (o.mCategories != null) mCategories = new ArrayList<String>(o.mCategories);
        if (o.mDataSchemes != null) mDataSchemes = new ArrayList<String>(o.mDataSchemes);
        if (o.mDataSchemeSpecificParts != null) {
            mDataSchemeSpecificParts = new ArrayList<PatternMatcher>(o.mDataSchemeSpecificParts);
        }
        if (o.mDataAuthorities != null) mDataAuthorities = new ArrayList<AuthorityEntry>(o.mDataAuthorities);
        if (o.mDataPaths != null) mDataPaths = new ArrayList<PatternMatcher>(o.mDataPaths);
        if (o.mDataTypes != null) mDataTypes = new ArrayList<String>(o.mDataTypes);
        mHasPartialTypes = o.mHasPartialTypes;
    }

    public static IntentFilter create(String action, String dataType) {
        try {
            return new IntentFilter(action, dataType);
        } catch (MalformedMimeTypeException e) {
            throw new RuntimeException("Bad MIME type", e);
        }
    }

    public final void setPriority(int priority) {
        mPriority = priority;
    }

    public final int getPriority() {
        return mPriority;
    }

    public final void setOrder(int order) {
        mOrder = order;
    }

    public final int getOrder() {
        return mOrder;
    }

    public final void addAction(String action) {
        if (!mActions.contains(action)) mActions.add(action.intern());
    }

    public final int countActions() {
        return mActions.size();
    }

    public final String getAction(int index) {
        return mActions.get(index);
    }

    public final boolean hasAction(String action) {
        return action != null && mActions.contains(action);
    }

    public final boolean matchAction(String action) {
        return hasAction(action);
    }

    public final Iterator<String> actionsIterator() {
        return mActions.iterator();
    }

    public final void addDataType(String type) throws MalformedMimeTypeException {
        final int slashpos = type.indexOf('/');
        final int typelen = type.length();
        if (slashpos > 0 && typelen >= slashpos + 2) {
            if (mDataTypes == null) mDataTypes = new ArrayList<String>();
            if (typelen == slashpos + 2 && type.charAt(slashpos + 1) == '*') {
                String str = type.substring(0, slashpos);
                if (!mDataTypes.contains(str)) mDataTypes.add(str.intern());
                mHasPartialTypes = true;
            } else if (!mDataTypes.contains(type)) {
                mDataTypes.add(type.intern());
            }
            return;
        }
        throw new MalformedMimeTypeException(type);
    }

    public final boolean hasDataType(String type) {
        return mDataTypes != null && findMimeType(type);
    }

    public final int countDataTypes() {
        return mDataTypes != null ? mDataTypes.size() : 0;
    }

    public final String getDataType(int index) {
        return mDataTypes.get(index);
    }

    public final void addDataScheme(String scheme) {
        if (mDataSchemes == null) mDataSchemes = new ArrayList<String>();
        if (!mDataSchemes.contains(scheme)) mDataSchemes.add(scheme.intern());
    }

    public final int countDataSchemes() {
        return mDataSchemes != null ? mDataSchemes.size() : 0;
    }

    public final String getDataScheme(int index) {
        return mDataSchemes.get(index);
    }

    public final boolean hasDataScheme(String scheme) {
        return mDataSchemes != null && mDataSchemes.contains(scheme);
    }

    public final void addDataSchemeSpecificPart(String ssp, int type) {
        if (mDataSchemeSpecificParts == null) mDataSchemeSpecificParts = new ArrayList<PatternMatcher>();
        mDataSchemeSpecificParts.add(new PatternMatcher(ssp, type));
    }

    public final void addDataAuthority(String host, String port) {
        if (port != null) port = port.intern();
        if (mDataAuthorities == null) mDataAuthorities = new ArrayList<AuthorityEntry>();
        mDataAuthorities.add(new AuthorityEntry(host.intern(), port));
    }

    public final int countDataAuthorities() {
        return mDataAuthorities != null ? mDataAuthorities.size() : 0;
    }

    public final AuthorityEntry getDataAuthority(int index) {
        return mDataAuthorities.get(index);
    }

    public final void addDataPath(String path, int type) {
        if (mDataPaths == null) mDataPaths = new ArrayList<PatternMatcher>();
        mDataPaths.add(new PatternMatcher(path.intern(), type));
    }

    public final int countDataPaths() {
        return mDataPaths != null ? mDataPaths.size() : 0;
    }

    public final PatternMatcher getDataPath(int index) {
        return mDataPaths.get(index);
    }

    public final void addCategory(String category) {
        if (mCategories == null) mCategories = new ArrayList<String>();
        if (!mCategories.contains(category)) mCategories.add(category.intern());
    }

    public final int countCategories() {
        return mCategories != null ? mCategories.size() : 0;
    }

    public final String getCategory(int index) {
        return mCategories.get(index);
    }

    public final boolean hasCategory(String category) {
        return mCategories != null && mCategories.contains(category);
    }

    public final Iterator<String> categoriesIterator() {
        return mCategories != null ? mCategories.iterator() : null;
    }

    public final String matchCategories(Set<String> categories) {
        if (categories == null) return null;
        for (String category : categories) {
            if (mCategories == null || !mCategories.contains(category)) return category;
        }
        return null;
    }

    private boolean findMimeType(String type) {
        final ArrayList<String> t = mDataTypes;
        if (type == null) return false;
        if (t.contains(type)) return true;
        final int typeLength = type.length();
        if (typeLength == 3 && type.equals("*/*")) return !t.isEmpty();
        if (mHasPartialTypes && t.contains("*")) return true;
        final int slashpos = type.indexOf('/');
        if (slashpos > 0) {
            if (mHasPartialTypes && t.contains(type.substring(0, slashpos))) return true;
            if (typeLength == slashpos + 2 && type.charAt(slashpos + 1) == '*') {
                for (String v : t) if (type.regionMatches(0, v, 0, slashpos + 1)) return true;
            }
        }
        return false;
    }

    public final int matchData(String type, String scheme, Uri data) {
        final ArrayList<String> types = mDataTypes;
        final ArrayList<String> schemes = mDataSchemes;
        int match = MATCH_CATEGORY_EMPTY;
        if (types == null && schemes == null) {
            return (type == null && data == null) ? (MATCH_CATEGORY_EMPTY + MATCH_ADJUSTMENT_NORMAL) : NO_MATCH_DATA;
        }
        if (schemes != null) {
            if (schemes.contains(scheme != null ? scheme : "")) {
                match = MATCH_CATEGORY_SCHEME;
            } else {
                return NO_MATCH_DATA;
            }
            if (mDataSchemeSpecificParts != null && data != null) {
                boolean ok = false;
                for (PatternMatcher pm : mDataSchemeSpecificParts) {
                    if (pm.match(data.getSchemeSpecificPart())) {
                        ok = true;
                        match = MATCH_CATEGORY_SCHEME_SPECIFIC_PART;
                        break;
                    }
                }
                if (!ok) return NO_MATCH_DATA;
            }
            if (mDataAuthorities != null && match != MATCH_CATEGORY_SCHEME_SPECIFIC_PART) {
                int best = NO_MATCH_DATA;
                for (AuthorityEntry ae : mDataAuthorities) {
                    int m = data != null ? ae.match(data) : NO_MATCH_DATA;
                    if (m >= 0) best = Math.max(best, m);
                }
                if (best < 0) return NO_MATCH_DATA;
                match = best;
                if (mDataPaths != null) {
                    boolean ok = false;
                    String path = data.getPath();
                    for (PatternMatcher pm : mDataPaths) {
                        if (path != null && pm.match(path)) {
                            ok = true;
                            break;
                        }
                    }
                    if (!ok) return NO_MATCH_DATA;
                    match = MATCH_CATEGORY_PATH;
                }
            }
            if (match == NO_MATCH_DATA) return NO_MATCH_DATA;
        } else {
            if (scheme != null && !"".equals(scheme) && !"content".equals(scheme) && !"file".equals(scheme)) {
                return NO_MATCH_DATA;
            }
        }
        if (types != null) {
            if (findMimeType(type)) {
                match = MATCH_CATEGORY_TYPE;
            } else {
                return NO_MATCH_TYPE;
            }
        } else if (type != null) {
            return NO_MATCH_TYPE;
        }
        return match + MATCH_ADJUSTMENT_NORMAL;
    }

    public final int match(String action, String type, String scheme, Uri data, Set<String> categories,
            String logTag) {
        if (action != null && !matchAction(action)) return NO_MATCH_ACTION;
        if (action == null && mActions.isEmpty()) return NO_MATCH_ACTION;
        int dataMatch = matchData(type, scheme, data);
        if (dataMatch < 0) return dataMatch;
        String categoryMismatch = matchCategories(categories);
        if (categoryMismatch != null) return NO_MATCH_CATEGORY;
        return dataMatch;
    }

    public final int match(ContentResolver resolver, Intent intent, boolean resolve, String logTag) {
        String type = resolve ? intent.resolveType(null) : intent.getType();
        return match(intent.getAction(), type, intent.getScheme(), intent.getData(), intent.getCategories(), logTag);
    }

    // --- parceling ---------------------------------------------------------------

    public static final Parcelable.Creator<IntentFilter> CREATOR = new Parcelable.Creator<IntentFilter>() {
        public IntentFilter createFromParcel(Parcel source) {
            IntentFilter f = new IntentFilter();
            f.mPriority = source.readInt();
            for (String a : source.createStringArrayList()) f.addAction(a);
            ArrayList<String> cats = source.createStringArrayList();
            if (cats != null) for (String c : cats) f.addCategory(c);
            ArrayList<String> schemes = source.createStringArrayList();
            if (schemes != null) for (String s : schemes) f.addDataScheme(s);
            ArrayList<String> types = source.createStringArrayList();
            if (types != null) {
                f.mDataTypes = types;
                f.mHasPartialTypes = source.readInt() != 0;
            } else {
                source.readInt();
            }
            return f;
        }

        public IntentFilter[] newArray(int size) {
            return new IntentFilter[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(mPriority);
        dest.writeStringList(mActions);
        dest.writeStringList(mCategories);
        dest.writeStringList(mDataSchemes);
        dest.writeStringList(mDataTypes);
        dest.writeInt(mHasPartialTypes ? 1 : 0);
    }

    @Override
    public String toString() {
        return "IntentFilter{actions=" + mActions + " categories=" + mCategories + " schemes=" + mDataSchemes
                + " types=" + mDataTypes + "}";
    }
}
