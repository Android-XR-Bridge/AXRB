package java.io;

import java.util.ArrayList;

/**
 * A guest path. The working directory of an Android process is "/", so
 * relative paths resolve against it. Operations go through the runtime's VFS
 * (see Vfs); nothing on the host outside the configured mounts is visible.
 */
public class File implements Serializable, Comparable<File> {
    public static final char separatorChar = '/';
    public static final String separator = "/";
    public static final char pathSeparatorChar = ':';
    public static final String pathSeparator = ":";

    private final String path;

    public File(String pathname) {
        if (pathname == null) throw new NullPointerException("pathname == null");
        this.path = fixSlashes(pathname);
    }

    public File(String parent, String child) {
        if (child == null) throw new NullPointerException("child == null");
        this.path = parent == null ? fixSlashes(child) : join(fixSlashes(parent), fixSlashes(child));
    }

    public File(File parent, String child) {
        this(parent == null ? null : parent.path, child);
    }

    private static String join(String prefix, String suffix) {
        if (prefix.isEmpty()) return suffix;
        if (suffix.isEmpty()) return prefix;
        boolean slash = prefix.endsWith("/");
        if (suffix.startsWith("/")) return slash ? prefix + suffix.substring(1) : prefix + suffix;
        return slash ? prefix + suffix : prefix + "/" + suffix;
    }

    /** Collapses repeated slashes and drops a trailing one (except for "/"). */
    private static String fixSlashes(String p) {
        StringBuilder sb = new StringBuilder(p.length());
        boolean lastSlash = false;
        for (int i = 0; i < p.length(); i++) {
            char c = p.charAt(i);
            if (c == '/') {
                if (lastSlash) continue;
                lastSlash = true;
            } else {
                lastSlash = false;
            }
            sb.append(c);
        }
        if (sb.length() > 1 && lastSlash) sb.setLength(sb.length() - 1);
        return sb.toString();
    }

    public String getName() {
        int i = path.lastIndexOf('/');
        return i < 0 ? path : path.substring(i + 1);
    }

    public String getParent() {
        int i = path.lastIndexOf('/');
        if (i < 0) return null;
        if (i == 0) return path.length() > 1 ? "/" : null;
        return path.substring(0, i);
    }

    public File getParentFile() {
        String p = getParent();
        return p == null ? null : new File(p);
    }

    public String getPath() {
        return path;
    }

    public boolean isAbsolute() {
        return path.startsWith("/");
    }

    public String getAbsolutePath() {
        return isAbsolute() ? path : path.isEmpty() ? "/" : "/" + path;
    }

    public File getAbsoluteFile() {
        return new File(getAbsolutePath());
    }

    /** Resolves "." and ".." lexically; the VFS has no symbolic links. */
    public String getCanonicalPath() throws IOException {
        String[] parts = getAbsolutePath().split("/");
        ArrayList<String> out = new ArrayList<String>();
        for (String s : parts) {
            if (s.isEmpty() || s.equals(".")) continue;
            if (s.equals("..")) {
                if (!out.isEmpty()) out.remove(out.size() - 1);
                continue;
            }
            out.add(s);
        }
        StringBuilder sb = new StringBuilder();
        for (String s : out) sb.append('/').append(s);
        return sb.length() == 0 ? "/" : sb.toString();
    }

    public File getCanonicalFile() throws IOException {
        return new File(getCanonicalPath());
    }

    public boolean exists() {
        return Vfs.access(getAbsolutePath(), Vfs.ACCESS_EXISTS);
    }

    public boolean canRead() {
        return Vfs.access(getAbsolutePath(), Vfs.ACCESS_READ);
    }

    public boolean canWrite() {
        return Vfs.access(getAbsolutePath(), Vfs.ACCESS_WRITE);
    }

    public boolean isDirectory() {
        return Vfs.isDirectory(getAbsolutePath());
    }

    public boolean isFile() {
        return Vfs.isFile(getAbsolutePath());
    }

    public boolean isHidden() {
        return getName().startsWith(".");
    }

    public long length() {
        return Vfs.length(getAbsolutePath());
    }

    public long lastModified() {
        return Vfs.lastModified(getAbsolutePath());
    }

    public boolean createNewFile() throws IOException {
        int h = Vfs.open(getAbsolutePath(), true, false, true, true);
        if (h == -17) return false;  // EEXIST
        if (h < 0) throw new IOException(path + ": open failed: " + Vfs.strerror(-h));
        Vfs.close(h);
        return true;
    }

    public boolean delete() {
        return Vfs.remove(getAbsolutePath()) == 0;
    }

    public boolean mkdir() {
        return Vfs.mkdir(getAbsolutePath()) == 0;
    }

    public boolean mkdirs() {
        if (exists()) return false;
        if (mkdir()) return true;
        File parent = getParentFile();
        return parent != null && (parent.mkdirs() || parent.exists()) && mkdir();
    }

    public boolean renameTo(File dest) {
        return Vfs.rename(getAbsolutePath(), dest.getAbsolutePath()) == 0;
    }

    public String[] list() {
        return Vfs.list(getAbsolutePath());
    }

    public File[] listFiles() {
        String[] names = list();
        if (names == null) return null;
        File[] files = new File[names.length];
        for (int i = 0; i < names.length; i++) files[i] = new File(this, names[i]);
        return files;
    }

    public File[] listFiles(FileFilter filter) {
        File[] all = listFiles();
        if (all == null || filter == null) return all;
        ArrayList<File> out = new ArrayList<File>();
        for (File f : all) {
            if (filter.accept(f)) out.add(f);
        }
        return out.toArray(new File[0]);
    }

    public int compareTo(File other) {
        return path.compareTo(other.path);
    }

    public boolean equals(Object obj) {
        return obj instanceof File && path.equals(((File) obj).path);
    }

    public int hashCode() {
        return path.hashCode() ^ 1234321;
    }

    public String toString() {
        return path;
    }
}
