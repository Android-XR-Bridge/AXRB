package java.net;

import java.io.IOException;
import java.io.InputStream;

public abstract class HttpURLConnection extends URLConnection {
    public static final int HTTP_OK = 200;
    public static final int HTTP_CREATED = 201;
    public static final int HTTP_ACCEPTED = 202;
    public static final int HTTP_NO_CONTENT = 204;
    public static final int HTTP_PARTIAL = 206;
    public static final int HTTP_MOVED_PERM = 301;
    public static final int HTTP_MOVED_TEMP = 302;
    public static final int HTTP_SEE_OTHER = 303;
    public static final int HTTP_NOT_MODIFIED = 304;
    public static final int HTTP_BAD_REQUEST = 400;
    public static final int HTTP_UNAUTHORIZED = 401;
    public static final int HTTP_FORBIDDEN = 403;
    public static final int HTTP_NOT_FOUND = 404;
    public static final int HTTP_INTERNAL_ERROR = 500;
    public static final int HTTP_UNAVAILABLE = 503;

    private static boolean followRedirects = true;

    protected String method = "GET";
    protected int responseCode = -1;
    protected String responseMessage;
    protected boolean instanceFollowRedirects = followRedirects;

    protected HttpURLConnection(URL u) {
        super(u);
    }

    public static void setFollowRedirects(boolean set) {
        followRedirects = set;
    }

    public static boolean getFollowRedirects() {
        return followRedirects;
    }

    public void setInstanceFollowRedirects(boolean followRedirects) {
        instanceFollowRedirects = followRedirects;
    }

    public boolean getInstanceFollowRedirects() {
        return instanceFollowRedirects;
    }

    public void setRequestMethod(String method) throws ProtocolException {
        if (connected) throw new ProtocolException("Can't reset method: already connected");
        String[] methods = {"GET", "POST", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE", "PATCH"};
        for (String m : methods) {
            if (m.equals(method)) {
                this.method = method;
                return;
            }
        }
        throw new ProtocolException("Invalid HTTP method: " + method);
    }

    public String getRequestMethod() {
        return method;
    }

    public int getResponseCode() throws IOException {
        if (responseCode != -1) return responseCode;
        getInputStream();
        return responseCode;
    }

    public String getResponseMessage() throws IOException {
        getResponseCode();
        return responseMessage;
    }

    public InputStream getErrorStream() {
        return null;
    }

    public abstract void disconnect();

    public abstract boolean usingProxy();
}
