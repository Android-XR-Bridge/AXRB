package java.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Java code has no network in this runtime (native code does, through the
 * runner's sockets): every request fails like on a device without a network.
 */
final class OfflineHttpURLConnection extends HttpURLConnection {
    OfflineHttpURLConnection(URL url) {
        super(url);
    }

    public void connect() throws IOException {
        throw new UnknownHostException("Unable to resolve host \"" + url.getHost() + "\": No address associated with hostname");
    }

    public InputStream getInputStream() throws IOException {
        connect();
        return null;
    }

    public OutputStream getOutputStream() throws IOException {
        connect();
        return null;
    }

    public void disconnect() {}

    public boolean usingProxy() {
        return false;
    }
}
