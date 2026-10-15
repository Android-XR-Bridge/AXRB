package java.util;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

/** Line and whitespace-token scanning over text (UTF-8). */
public final class Scanner implements java.io.Closeable, Iterator<String> {
    private final String text;
    private int pos;
    private String delimiter;

    public Scanner(String source) {
        text = source;
    }

    public Scanner(InputStream source) {
        this(source, "UTF-8");
    }

    public Scanner(InputStream source, String charsetName) {
        text = readAll(source);
    }

    public Scanner(File source) throws FileNotFoundException {
        this(new FileInputStream(source));
    }

    private static String readAll(InputStream in) {
        try {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf, 0, buf.length)) > 0) out.write(buf, 0, n);
            return new String(out.toByteArray(), "UTF-8");
        } catch (IOException e) {
            return "";
        }
    }

    public Scanner useDelimiter(String pattern) {
        // Only "\\A" (whole input) and single literal characters are supported.
        delimiter = pattern;
        return this;
    }

    private boolean isDelimiter(char c) {
        if (delimiter == null) return Character.isWhitespace(c);
        if (delimiter.equals("\\A")) return false;
        return delimiter.length() == 1 && c == delimiter.charAt(0);
    }

    public boolean hasNextLine() {
        return pos < text.length();
    }

    public String nextLine() {
        if (pos >= text.length()) throw new NoSuchElementException("No line found");
        int end = text.indexOf('\n', pos);
        String line;
        if (end < 0) {
            line = text.substring(pos);
            pos = text.length();
        } else {
            line = text.substring(pos, end);
            pos = end + 1;
        }
        if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
        return line;
    }

    private void skipDelimiters() {
        while (pos < text.length() && isDelimiter(text.charAt(pos))) pos++;
    }

    public boolean hasNext() {
        skipDelimiters();
        return pos < text.length();
    }

    public String next() {
        skipDelimiters();
        if (pos >= text.length()) throw new NoSuchElementException();
        int start = pos;
        while (pos < text.length() && !isDelimiter(text.charAt(pos))) pos++;
        return text.substring(start, pos);
    }

    public boolean hasNextInt() {
        int save = pos;
        try {
            if (!hasNext()) return false;
            Integer.parseInt(next());
            return true;
        } catch (NumberFormatException e) {
            return false;
        } finally {
            pos = save;
        }
    }

    public int nextInt() {
        String t = next();
        try {
            return Integer.parseInt(t);
        } catch (NumberFormatException e) {
            throw new InputMismatchException(t);
        }
    }

    public long nextLong() {
        String t = next();
        try {
            return Long.parseLong(t);
        } catch (NumberFormatException e) {
            throw new InputMismatchException(t);
        }
    }

    public void remove() {
        throw new UnsupportedOperationException();
    }

    public void close() {}
}
