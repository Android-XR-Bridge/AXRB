package java.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public abstract class URLConnection {
    protected URL url;
    protected boolean doInput = true;
    protected boolean doOutput;
    protected boolean useCaches = true;
    protected boolean connected;
    private int connectTimeout;
    private int readTimeout;
    private final LinkedHashMap<String, List<String>> requestProperties = new LinkedHashMap<String, List<String>>();

    protected URLConnection(URL url) {
        this.url = url;
    }

    public abstract void connect() throws IOException;

    public URL getURL() {
        return url;
    }

    public void setConnectTimeout(int timeout) {
        if (timeout < 0) throw new IllegalArgumentException("timeouts can't be negative");
        connectTimeout = timeout;
    }

    public int getConnectTimeout() {
        return connectTimeout;
    }

    public void setReadTimeout(int timeout) {
        if (timeout < 0) throw new IllegalArgumentException("timeouts can't be negative");
        readTimeout = timeout;
    }

    public int getReadTimeout() {
        return readTimeout;
    }

    public void setDoInput(boolean doinput) {
        doInput = doinput;
    }

    public boolean getDoInput() {
        return doInput;
    }

    public void setDoOutput(boolean dooutput) {
        doOutput = dooutput;
    }

    public boolean getDoOutput() {
        return doOutput;
    }

    public void setUseCaches(boolean usecaches) {
        useCaches = usecaches;
    }

    public boolean getUseCaches() {
        return useCaches;
    }

    public void setRequestProperty(String key, String value) {
        if (key == null) throw new NullPointerException("key is null");
        List<String> values = new ArrayList<String>();
        values.add(value);
        requestProperties.put(key, values);
    }

    public void addRequestProperty(String key, String value) {
        if (key == null) throw new NullPointerException("key is null");
        List<String> values = requestProperties.get(key);
        if (values == null) requestProperties.put(key, values = new ArrayList<String>());
        values.add(value);
    }

    public String getRequestProperty(String key) {
        List<String> values = requestProperties.get(key);
        return values == null || values.isEmpty() ? null : values.get(0);
    }

    public Map<String, List<String>> getRequestProperties() {
        return Collections.unmodifiableMap(requestProperties);
    }

    public String getHeaderField(String name) {
        return null;
    }

    public String getHeaderField(int n) {
        return null;
    }

    public String getHeaderFieldKey(int n) {
        return null;
    }

    public Map<String, List<String>> getHeaderFields() {
        return Collections.emptyMap();
    }

    public int getContentLength() {
        return -1;
    }

    public long getContentLengthLong() {
        return -1;
    }

    public String getContentType() {
        return getHeaderField("content-type");
    }

    public String getContentEncoding() {
        return getHeaderField("content-encoding");
    }

    public InputStream getInputStream() throws IOException {
        throw new UnknownServiceException("protocol doesn't support input");
    }

    public OutputStream getOutputStream() throws IOException {
        throw new UnknownServiceException("protocol doesn't support output");
    }

    public static String guessContentTypeFromName(String fname) {
        if (fname == null) return null;
        String n = fname.toLowerCase();
        int dot = n.lastIndexOf('.');
        String ext = dot < 0 ? "" : n.substring(dot + 1);
        if (ext.equals("html") || ext.equals("htm")) return "text/html";
        if (ext.equals("txt")) return "text/plain";
        if (ext.equals("json")) return "application/json";
        if (ext.equals("xml")) return "application/xml";
        if (ext.equals("png")) return "image/png";
        if (ext.equals("jpg") || ext.equals("jpeg")) return "image/jpeg";
        if (ext.equals("gif")) return "image/gif";
        if (ext.equals("zip")) return "application/zip";
        if (ext.equals("mp4")) return "video/mp4";
        return null;
    }

    public String toString() {
        return getClass().getName() + ":" + url;
    }
}
