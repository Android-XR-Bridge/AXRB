package java.lang;

/** Present so Runtime.exec compiles; no Process is ever created (spec 10.3). */
public abstract class Process {
    public Process() {}

    public abstract java.io.OutputStream getOutputStream();

    public abstract java.io.InputStream getInputStream();

    public abstract java.io.InputStream getErrorStream();

    public abstract int waitFor() throws InterruptedException;

    public abstract int exitValue();

    public abstract void destroy();
}
