package java.net;

/** URL parsing; connections are offline (see URLConnection). */
public final class URL implements java.io.Serializable {
    private final String protocol;
    private final String host;
    private final int port;
    private final String file;
    private final String ref;
    private final String userInfo;

    public URL(String spec) throws MalformedURLException {
        this(null, spec);
    }

    public URL(URL context, String spec) throws MalformedURLException {
        if (spec == null) throw new MalformedURLException("null spec");
        String s = spec.trim();
        int colon = s.indexOf(':');
        int slash = s.indexOf('/');
        String scheme = null;
        if (colon > 0 && (slash < 0 || colon < slash)) {
            scheme = s.substring(0, colon).toLowerCase();
            s = s.substring(colon + 1);
        }
        if (scheme == null) {
            if (context == null) throw new MalformedURLException("no protocol: " + spec);
            scheme = context.protocol;
        }
        String hostPart = null, path = s, fragment = null;
        int hash = path.indexOf('#');
        if (hash >= 0) {
            fragment = path.substring(hash + 1);
            path = path.substring(0, hash);
        }
        if (path.startsWith("//")) {
            int end = path.indexOf('/', 2);
            int q = path.indexOf('?', 2);
            if (end < 0 || (q >= 0 && q < end)) end = q;
            hostPart = end < 0 ? path.substring(2) : path.substring(2, end);
            path = end < 0 ? "" : path.substring(end);
        } else if (context != null && scheme.equals(context.protocol)) {
            hostPart = context.getAuthority();
            if (!path.startsWith("/")) {
                String base = context.getPath();
                int last = base.lastIndexOf('/');
                path = (last >= 0 ? base.substring(0, last + 1) : "/") + path;
            }
        }
        String user = null, h = "";
        int p = -1;
        if (hostPart != null) {
            int at = hostPart.lastIndexOf('@');
            if (at >= 0) {
                user = hostPart.substring(0, at);
                hostPart = hostPart.substring(at + 1);
            }
            int pc = hostPart.lastIndexOf(':');
            if (pc >= 0 && hostPart.indexOf(']') < pc) {
                String ps = hostPart.substring(pc + 1);
                hostPart = hostPart.substring(0, pc);
                if (!ps.isEmpty()) {
                    try {
                        p = Integer.parseInt(ps);
                    } catch (NumberFormatException e) {
                        throw new MalformedURLException("invalid port: " + ps);
                    }
                }
            }
            h = hostPart;
        }
        this.protocol = scheme;
        this.host = h;
        this.port = p;
        this.file = path;
        this.ref = fragment;
        this.userInfo = user;
    }

    public URL(String protocol, String host, int port, String file) throws MalformedURLException {
        if (protocol == null) throw new MalformedURLException("null protocol");
        this.protocol = protocol.toLowerCase();
        this.host = host == null ? "" : host;
        this.port = port;
        int hash = file.indexOf('#');
        this.file = hash < 0 ? file : file.substring(0, hash);
        this.ref = hash < 0 ? null : file.substring(hash + 1);
        this.userInfo = null;
    }

    public URL(String protocol, String host, String file) throws MalformedURLException {
        this(protocol, host, -1, file);
    }

    public String getProtocol() {
        return protocol;
    }

    public String getHost() {
        return host;
    }

    public int getPort() {
        return port;
    }

    public int getDefaultPort() {
        if (protocol.equals("http")) return 80;
        if (protocol.equals("https")) return 443;
        if (protocol.equals("ftp")) return 21;
        return -1;
    }

    public String getFile() {
        return file;
    }

    public String getPath() {
        int q = file.indexOf('?');
        return q < 0 ? file : file.substring(0, q);
    }

    public String getQuery() {
        int q = file.indexOf('?');
        return q < 0 ? null : file.substring(q + 1);
    }

    public String getRef() {
        return ref;
    }

    public String getUserInfo() {
        return userInfo;
    }

    public String getAuthority() {
        if (host.isEmpty() && userInfo == null) return host;
        return (userInfo != null ? userInfo + "@" : "") + host + (port >= 0 ? ":" + port : "");
    }

    public URLConnection openConnection() throws java.io.IOException {
        if (protocol.equals("http") || protocol.equals("https")) return new OfflineHttpURLConnection(this);
        throw new java.io.IOException("unsupported protocol: " + protocol);
    }

    public java.io.InputStream openStream() throws java.io.IOException {
        return openConnection().getInputStream();
    }

    public URI toURI() throws URISyntaxException {
        return new URI(toString());
    }

    public String toExternalForm() {
        StringBuilder sb = new StringBuilder(protocol).append(':');
        String authority = getAuthority();
        if (!authority.isEmpty() || protocol.equals("http") || protocol.equals("https")) sb.append("//").append(authority);
        sb.append(file);
        if (ref != null) sb.append('#').append(ref);
        return sb.toString();
    }

    public String toString() {
        return toExternalForm();
    }

    public boolean equals(Object o) {
        return o instanceof URL && toExternalForm().equals(((URL) o).toExternalForm());
    }

    public int hashCode() {
        return toExternalForm().hashCode();
    }
}
