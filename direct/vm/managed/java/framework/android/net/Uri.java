package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.io.File;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * An immutable URI reference (RFC 2396 style, as android.net.Uri): parsed
 * lazily from its string form, or built from parts. Components are returned
 * decoded; getEncoded* return them as they appear in the string.
 */
public abstract class Uri implements Parcelable, Comparable<Uri> {
    public static final Uri EMPTY = new StringUri("");

    private static final char[] HEX_DIGITS = "0123456789ABCDEF".toCharArray();
    private static final String DEFAULT_ENCODING = "UTF-8";

    Uri() {}

    public abstract boolean isHierarchical();

    public boolean isOpaque() {
        return !isHierarchical();
    }

    public abstract boolean isRelative();

    public boolean isAbsolute() {
        return !isRelative();
    }

    public abstract String getScheme();

    public abstract String getSchemeSpecificPart();

    public abstract String getEncodedSchemeSpecificPart();

    public abstract String getAuthority();

    public abstract String getEncodedAuthority();

    public abstract String getUserInfo();

    public abstract String getEncodedUserInfo();

    public abstract String getHost();

    public abstract int getPort();

    public abstract String getPath();

    public abstract String getEncodedPath();

    public abstract String getQuery();

    public abstract String getEncodedQuery();

    public abstract String getFragment();

    public abstract String getEncodedFragment();

    public abstract List<String> getPathSegments();

    public abstract String getLastPathSegment();

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Uri)) return false;
        return toString().equals(o.toString());
    }

    @Override
    public int hashCode() {
        return toString().hashCode();
    }

    @Override
    public int compareTo(Uri other) {
        return toString().compareTo(other.toString());
    }

    @Override
    public abstract String toString();

    public abstract Builder buildUpon();

    public Uri normalizeScheme() {
        String scheme = getScheme();
        if (scheme == null) return this;
        String lower = scheme.toLowerCase(Locale.ROOT);
        if (scheme.equals(lower)) return this;
        return buildUpon().scheme(lower).build();
    }

    // --- factories ---------------------------------------------------------------

    public static Uri parse(String uriString) {
        return new StringUri(uriString);
    }

    public static Uri fromFile(File file) {
        if (file == null) throw new NullPointerException("file");
        return new Builder().scheme("file").authority("").path(file.getAbsolutePath()).build();
    }

    public static Uri fromParts(String scheme, String ssp, String fragment) {
        if (scheme == null) throw new NullPointerException("scheme");
        if (ssp == null) throw new NullPointerException("ssp");
        StringBuilder b = new StringBuilder(scheme).append(':').append(encode(ssp, null));
        if (fragment != null) b.append('#').append(encode(fragment, null));
        return new StringUri(b.toString());
    }

    public static Uri withAppendedPath(Uri baseUri, String pathSegment) {
        return baseUri.buildUpon().appendEncodedPath(pathSegment).build();
    }

    // --- query parameters ------------------------------------------------------------

    public Set<String> getQueryParameterNames() {
        if (isOpaque()) throw new UnsupportedOperationException("This isn't a hierarchical URI.");
        String query = getEncodedQuery();
        if (query == null) return Collections.emptySet();
        Set<String> names = new LinkedHashSet<String>();
        int start = 0;
        do {
            int next = query.indexOf('&', start);
            int end = (next == -1) ? query.length() : next;
            int separator = query.indexOf('=', start);
            if (separator > end || separator == -1) separator = end;
            names.add(decode(query.substring(start, separator)));
            start = end + 1;
        } while (start < query.length());
        return Collections.unmodifiableSet(names);
    }

    public List<String> getQueryParameters(String key) {
        if (isOpaque()) throw new UnsupportedOperationException("This isn't a hierarchical URI.");
        if (key == null) throw new NullPointerException("key");
        String query = getEncodedQuery();
        if (query == null) return Collections.emptyList();
        String encodedKey = encode(key, null);
        ArrayList<String> values = new ArrayList<String>();
        int start = 0;
        do {
            int nextAmpersand = query.indexOf('&', start);
            int end = nextAmpersand != -1 ? nextAmpersand : query.length();
            int separator = query.indexOf('=', start);
            if (separator > end || separator == -1) separator = end;
            if (separator - start == encodedKey.length() && query.regionMatches(start, encodedKey, 0, encodedKey.length())) {
                if (separator == end) {
                    values.add("");
                } else {
                    values.add(decode(query.substring(separator + 1, end)));
                }
            }
            if (nextAmpersand != -1) start = nextAmpersand + 1; else break;
        } while (true);
        return Collections.unmodifiableList(values);
    }

    public String getQueryParameter(String key) {
        if (isOpaque()) throw new UnsupportedOperationException("This isn't a hierarchical URI.");
        if (key == null) throw new NullPointerException("key");
        final String query = getEncodedQuery();
        if (query == null) return null;
        final String encodedKey = encode(key, null);
        final int length = query.length();
        int start = 0;
        do {
            int nextAmpersand = query.indexOf('&', start);
            int end = nextAmpersand != -1 ? nextAmpersand : length;
            int separator = query.indexOf('=', start);
            if (separator > end || separator == -1) separator = end;
            if (separator - start == encodedKey.length() && query.regionMatches(start, encodedKey, 0, encodedKey.length())) {
                if (separator == end) return "";
                String encodedValue = query.substring(separator + 1, end);
                return decode(encodedValue.replace('+', ' '));
            }
            if (nextAmpersand != -1) start = nextAmpersand + 1; else break;
        } while (true);
        return null;
    }

    public boolean getBooleanQueryParameter(String key, boolean defaultValue) {
        String flag = getQueryParameter(key);
        if (flag == null) return defaultValue;
        flag = flag.toLowerCase(Locale.ROOT);
        return !"false".equals(flag) && !"0".equals(flag);
    }

    // --- encoding --------------------------------------------------------------------

    public static String encode(String s) {
        return encode(s, null);
    }

    private static boolean isAllowed(char c, String allow) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || "_-!.~'()*".indexOf(c) != -1 || (allow != null && allow.indexOf(c) != -1);
    }

    public static String encode(String s, String allow) {
        if (s == null) return null;
        StringBuilder encoded = null;
        int oldLength = s.length();
        int current = 0;
        while (current < oldLength) {
            int nextToEncode = current;
            while (nextToEncode < oldLength && isAllowed(s.charAt(nextToEncode), allow)) nextToEncode++;
            if (nextToEncode == oldLength) {
                if (current == 0) return s;
                encoded.append(s, current, oldLength);
                return encoded.toString();
            }
            if (encoded == null) encoded = new StringBuilder();
            if (nextToEncode > current) encoded.append(s, current, nextToEncode);
            current = nextToEncode;
            int nextAllowed = current + 1;
            while (nextAllowed < oldLength && !isAllowed(s.charAt(nextAllowed), allow)) nextAllowed++;
            String toEncode = s.substring(current, nextAllowed);
            try {
                byte[] bytes = toEncode.getBytes(DEFAULT_ENCODING);
                for (byte b : bytes) {
                    encoded.append('%');
                    encoded.append(HEX_DIGITS[(b & 0xf0) >> 4]);
                    encoded.append(HEX_DIGITS[b & 0xf]);
                }
            } catch (UnsupportedEncodingException e) {
                throw new AssertionError(e);
            }
            current = nextAllowed;
        }
        return encoded == null ? s : encoded.toString();
    }

    public static String decode(String s) {
        if (s == null) return null;
        if (s.indexOf('%') < 0) return s;
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream(s.length());
        StringBuilder result = new StringBuilder(s.length());
        int i = 0;
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c == '%' && hex(s, i + 1) >= 0 && hex(s, i + 2) >= 0) {
                // A run of %XX escapes is one UTF-8 byte sequence.
                out.reset();
                while (i < s.length() && s.charAt(i) == '%' && hex(s, i + 1) >= 0 && hex(s, i + 2) >= 0) {
                    out.write((hex(s, i + 1) << 4) | hex(s, i + 2));
                    i += 3;
                }
                try {
                    result.append(out.toString(DEFAULT_ENCODING));
                } catch (UnsupportedEncodingException e) {
                    throw new AssertionError(e);
                }
            } else {
                result.append(c);
                i++;
            }
        }
        return result.toString();
    }

    private static int hex(String s, int i) {
        if (i >= s.length()) return -1;
        char c = s.charAt(i);
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // --- parceling ---------------------------------------------------------------------

    public static final Parcelable.Creator<Uri> CREATOR = new Parcelable.Creator<Uri>() {
        public Uri createFromParcel(Parcel in) {
            String s = in.readString();
            return s == null ? null : parse(s);
        }

        public Uri[] newArray(int size) {
            return new Uri[size];
        }
    };

    public static void writeToParcel(Parcel out, Uri uri) {
        out.writeString(uri == null ? null : uri.toString());
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeString(toString());
    }

    // --- the string-backed implementation ------------------------------------------------

    private static final class StringUri extends Uri {
        private final String uriString;
        private int cachedSsi = -2;
        private int cachedFsi = -2;

        StringUri(String uriString) {
            if (uriString == null) throw new NullPointerException("uriString");
            this.uriString = uriString;
        }

        private int findSchemeSeparator() {
            if (cachedSsi == -2) {
                int ssi = uriString.indexOf(':');
                // A scheme contains only [a-zA-Z0-9+.-] and appears before '/', '?' and '#'.
                if (ssi > 0) {
                    for (int i = 0; i < ssi; i++) {
                        char c = uriString.charAt(i);
                        boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                                || (i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'));
                        if (!ok) {
                            ssi = -1;
                            break;
                        }
                    }
                }
                cachedSsi = ssi;
            }
            return cachedSsi;
        }

        private int findFragmentSeparator() {
            if (cachedFsi == -2) cachedFsi = uriString.indexOf('#', Math.max(0, findSchemeSeparator()));
            return cachedFsi;
        }

        @Override
        public boolean isHierarchical() {
            int ssi = findSchemeSeparator();
            if (ssi <= 0) return true;
            if (uriString.length() == ssi + 1) return false;
            return uriString.charAt(ssi + 1) == '/';
        }

        @Override
        public boolean isRelative() {
            return findSchemeSeparator() <= 0;
        }

        @Override
        public String getScheme() {
            int ssi = findSchemeSeparator();
            return ssi <= 0 ? null : uriString.substring(0, ssi);
        }

        @Override
        public String getEncodedSchemeSpecificPart() {
            int ssi = findSchemeSeparator();
            int fsi = findFragmentSeparator();
            return fsi == -1 ? uriString.substring(ssi + 1) : uriString.substring(ssi + 1, fsi);
        }

        @Override
        public String getSchemeSpecificPart() {
            return decode(getEncodedSchemeSpecificPart());
        }

        private String hierarchicalPart() {
            if (!isHierarchical()) return null;
            String ssp = getEncodedSchemeSpecificPart();
            int q = ssp.indexOf('?');
            return q == -1 ? ssp : ssp.substring(0, q);
        }

        @Override
        public String getEncodedAuthority() {
            String h = hierarchicalPart();
            if (h == null || !h.startsWith("//")) return null;
            int end = 2;
            while (end < h.length() && h.charAt(end) != '/') end++;
            return h.substring(2, end);
        }

        @Override
        public String getAuthority() {
            return decode(getEncodedAuthority());
        }

        @Override
        public String getEncodedUserInfo() {
            String a = getEncodedAuthority();
            if (a == null) return null;
            int at = a.lastIndexOf('@');
            return at == -1 ? null : a.substring(0, at);
        }

        @Override
        public String getUserInfo() {
            return decode(getEncodedUserInfo());
        }

        @Override
        public String getHost() {
            String a = getEncodedAuthority();
            if (a == null) return null;
            int at = a.lastIndexOf('@');
            String hostPort = a.substring(at + 1);
            int colon;
            if (hostPort.startsWith("[")) {
                int close = hostPort.indexOf(']');
                colon = close >= 0 ? hostPort.indexOf(':', close) : -1;
            } else {
                colon = hostPort.lastIndexOf(':');
            }
            String host = colon == -1 ? hostPort : hostPort.substring(0, colon);
            return decode(host);
        }

        @Override
        public int getPort() {
            String a = getEncodedAuthority();
            if (a == null) return -1;
            int at = a.lastIndexOf('@');
            String hostPort = a.substring(at + 1);
            int colon = hostPort.lastIndexOf(':');
            if (colon == -1 || hostPort.indexOf(']', colon) != -1) return -1;
            try {
                return Integer.parseInt(decode(hostPort.substring(colon + 1)));
            } catch (NumberFormatException e) {
                return -1;
            }
        }

        @Override
        public String getEncodedPath() {
            String h = hierarchicalPart();
            if (h == null) return null;
            if (h.startsWith("//")) {
                int slash = h.indexOf('/', 2);
                return slash == -1 ? "" : h.substring(slash);
            }
            return h;
        }

        @Override
        public String getPath() {
            return decode(getEncodedPath());
        }

        @Override
        public String getEncodedQuery() {
            int ssi = findSchemeSeparator();
            int qsi = uriString.indexOf('?', Math.max(ssi, 0));
            if (qsi == -1) return null;
            int fsi = findFragmentSeparator();
            if (fsi != -1 && fsi < qsi) return null;
            return fsi == -1 ? uriString.substring(qsi + 1) : uriString.substring(qsi + 1, fsi);
        }

        @Override
        public String getQuery() {
            return decode(getEncodedQuery());
        }

        @Override
        public String getEncodedFragment() {
            int fsi = findFragmentSeparator();
            return fsi == -1 ? null : uriString.substring(fsi + 1);
        }

        @Override
        public String getFragment() {
            return decode(getEncodedFragment());
        }

        @Override
        public List<String> getPathSegments() {
            String path = getEncodedPath();
            if (path == null) return Collections.emptyList();
            ArrayList<String> segments = new ArrayList<String>();
            int previous = 0;
            int current;
            while ((current = path.indexOf('/', previous)) > -1) {
                if (previous < current) segments.add(decode(path.substring(previous, current)));
                previous = current + 1;
            }
            if (previous < path.length()) segments.add(decode(path.substring(previous)));
            return Collections.unmodifiableList(segments);
        }

        @Override
        public String getLastPathSegment() {
            List<String> segments = getPathSegments();
            return segments.isEmpty() ? null : segments.get(segments.size() - 1);
        }

        @Override
        public String toString() {
            return uriString;
        }

        @Override
        public Builder buildUpon() {
            if (isHierarchical()) {
                return new Builder().scheme(getScheme()).encodedAuthority(getEncodedAuthority())
                        .encodedPath(getEncodedPath()).encodedQuery(getEncodedQuery())
                        .encodedFragment(getEncodedFragment());
            }
            return new Builder().scheme(getScheme()).encodedOpaquePart(getEncodedSchemeSpecificPart())
                    .encodedFragment(getEncodedFragment());
        }
    }

    public static final class Builder {
        private String scheme;
        private String opaquePart;
        private String authority;
        private StringBuilder path;
        private StringBuilder query;
        private String fragment;

        public Builder() {}

        public Builder scheme(String scheme) {
            this.scheme = scheme;
            return this;
        }

        public Builder opaquePart(String opaquePart) {
            this.opaquePart = encode(opaquePart, null);
            return this;
        }

        public Builder encodedOpaquePart(String opaquePart) {
            this.opaquePart = opaquePart;
            return this;
        }

        public Builder authority(String authority) {
            opaquePart = null;
            this.authority = encode(authority, "@:[]");
            return this;
        }

        public Builder encodedAuthority(String authority) {
            opaquePart = null;
            this.authority = authority;
            return this;
        }

        public Builder path(String path) {
            opaquePart = null;
            this.path = path == null ? null : new StringBuilder(encode(path, "/"));
            return this;
        }

        public Builder encodedPath(String path) {
            opaquePart = null;
            this.path = path == null ? null : new StringBuilder(path);
            return this;
        }

        public Builder appendPath(String newSegment) {
            return appendEncodedPath(encode(newSegment, null));
        }

        public Builder appendEncodedPath(String newSegment) {
            opaquePart = null;
            if (path == null) path = new StringBuilder();
            if (newSegment == null || newSegment.length() == 0) return this;
            boolean endsWithSlash = path.length() > 0 && path.charAt(path.length() - 1) == '/';
            boolean startsWithSlash = newSegment.charAt(0) == '/';
            if (endsWithSlash && startsWithSlash) {
                path.append(newSegment, 1, newSegment.length());
            } else if (!endsWithSlash && !startsWithSlash) {
                path.append('/').append(newSegment);
            } else {
                path.append(newSegment);
            }
            return this;
        }

        public Builder query(String query) {
            opaquePart = null;
            this.query = query == null ? null : new StringBuilder(encode(query, null));
            return this;
        }

        public Builder encodedQuery(String query) {
            opaquePart = null;
            this.query = query == null ? null : new StringBuilder(query);
            return this;
        }

        public Builder fragment(String fragment) {
            this.fragment = encode(fragment, null);
            return this;
        }

        public Builder encodedFragment(String fragment) {
            this.fragment = fragment;
            return this;
        }

        public Builder appendQueryParameter(String key, String value) {
            opaquePart = null;
            String encodedParameter = encode(key, null) + "=" + encode(value, null);
            if (query == null) {
                query = new StringBuilder(encodedParameter);
            } else if (query.length() == 0) {
                query.append(encodedParameter);
            } else {
                query.append('&').append(encodedParameter);
            }
            return this;
        }

        public Builder clearQuery() {
            query = null;
            return this;
        }

        public Uri build() {
            StringBuilder b = new StringBuilder();
            if (scheme != null) b.append(scheme).append(':');
            if (opaquePart != null) {
                b.append(opaquePart);
            } else {
                if (authority != null) b.append("//").append(authority);
                if (path != null && path.length() > 0) {
                    if (authority != null && path.charAt(0) != '/') b.append('/');
                    b.append(path);
                }
                if (query != null) b.append('?').append(query);
            }
            if (fragment != null) b.append('#').append(fragment);
            return new StringUri(b.toString());
        }

        @Override
        public String toString() {
            return build().toString();
        }
    }
}
