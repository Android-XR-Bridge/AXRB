package java.net;

/** RFC 2396 component parsing without normalisation or relativisation. */
public final class URI implements Comparable<URI>, java.io.Serializable {
    private final String string;
    private String scheme;
    private String schemeSpecificPart;
    private String authority;
    private String userInfo;
    private String host;
    private int port = -1;
    private String path;
    private String query;
    private String fragment;

    public URI(String str) throws URISyntaxException {
        if (str == null) throw new NullPointerException();
        string = str;
        parse(str);
    }

    public URI(String scheme, String ssp, String fragment) throws URISyntaxException {
        this((scheme != null ? scheme + ":" : "") + (ssp != null ? ssp : "") + (fragment != null ? "#" + fragment : ""));
    }

    public URI(String scheme, String userInfo, String host, int port, String path, String query, String fragment)
            throws URISyntaxException {
        this(build(scheme, (host != null ? (userInfo != null ? userInfo + "@" : "") + host + (port >= 0 ? ":" + port : "")
                : null), path, query, fragment));
    }

    public URI(String scheme, String host, String path, String fragment) throws URISyntaxException {
        this(scheme, null, host, -1, path, null, fragment);
    }

    public URI(String scheme, String authority, String path, String query, String fragment) throws URISyntaxException {
        this(build(scheme, authority, path, query, fragment));
    }

    private static String build(String scheme, String authority, String path, String query, String fragment) {
        StringBuilder sb = new StringBuilder();
        if (scheme != null) sb.append(scheme).append(':');
        if (authority != null) sb.append("//").append(authority);
        if (path != null) sb.append(path);
        if (query != null) sb.append('?').append(query);
        if (fragment != null) sb.append('#').append(fragment);
        return sb.toString();
    }

    public static URI create(String str) {
        try {
            return new URI(str);
        } catch (URISyntaxException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        }
    }

    private void parse(String s) throws URISyntaxException {
        if (s.indexOf(' ') >= 0) throw new URISyntaxException(s, "Illegal character", s.indexOf(' '));
        int hash = s.indexOf('#');
        if (hash >= 0) {
            fragment = s.substring(hash + 1);
            s = s.substring(0, hash);
        }
        int colon = s.indexOf(':');
        int firstDelim = firstOf(s, "/?#");
        if (colon > 0 && (firstDelim < 0 || colon < firstDelim)) {
            scheme = s.substring(0, colon);
            s = s.substring(colon + 1);
        }
        schemeSpecificPart = s;
        if (scheme != null && !s.startsWith("/")) return;  // opaque
        int q = s.indexOf('?');
        if (q >= 0) {
            query = s.substring(q + 1);
            s = s.substring(0, q);
        }
        if (s.startsWith("//")) {
            int end = s.indexOf('/', 2);
            authority = end < 0 ? s.substring(2) : s.substring(2, end);
            s = end < 0 ? "" : s.substring(end);
            String a = authority;
            int at = a.lastIndexOf('@');
            if (at >= 0) {
                userInfo = a.substring(0, at);
                a = a.substring(at + 1);
            }
            int pc = a.lastIndexOf(':');
            if (pc >= 0 && a.indexOf(']') < pc) {
                String ps = a.substring(pc + 1);
                a = a.substring(0, pc);
                if (!ps.isEmpty()) {
                    try {
                        port = Integer.parseInt(ps);
                    } catch (NumberFormatException e) {
                        throw new URISyntaxException(string, "Illegal port", -1);
                    }
                }
            }
            host = a.isEmpty() ? null : a;
        }
        path = s;
    }

    private static int firstOf(String s, String chars) {
        for (int i = 0; i < s.length(); i++) {
            if (chars.indexOf(s.charAt(i)) >= 0) return i;
        }
        return -1;
    }

    public String getScheme() {
        return scheme;
    }

    public boolean isAbsolute() {
        return scheme != null;
    }

    public boolean isOpaque() {
        return path == null;
    }

    public String getRawSchemeSpecificPart() {
        return schemeSpecificPart;
    }

    public String getSchemeSpecificPart() {
        return decode(schemeSpecificPart);
    }

    public String getRawAuthority() {
        return authority;
    }

    public String getAuthority() {
        return decode(authority);
    }

    public String getRawUserInfo() {
        return userInfo;
    }

    public String getUserInfo() {
        return decode(userInfo);
    }

    public String getHost() {
        return host;
    }

    public int getPort() {
        return port;
    }

    public String getRawPath() {
        return path;
    }

    public String getPath() {
        return decode(path);
    }

    public String getRawQuery() {
        return query;
    }

    public String getQuery() {
        return decode(query);
    }

    public String getRawFragment() {
        return fragment;
    }

    public String getFragment() {
        return decode(fragment);
    }

    private static String decode(String s) {
        if (s == null || s.indexOf('%') < 0) return s;
        try {
            return URLDecoder.decode(s.replace("+", "%2B"), "UTF-8");
        } catch (java.io.UnsupportedEncodingException e) {
            return s;
        }
    }

    public URL toURL() throws MalformedURLException {
        if (!isAbsolute()) throw new IllegalArgumentException("URI is not absolute");
        return new URL(string);
    }

    public String toASCIIString() {
        return string;
    }

    public String toString() {
        return string;
    }

    public boolean equals(Object o) {
        return o instanceof URI && string.equals(((URI) o).string);
    }

    public int hashCode() {
        return string.hashCode();
    }

    public int compareTo(URI that) {
        return string.compareTo(that.string);
    }
}
