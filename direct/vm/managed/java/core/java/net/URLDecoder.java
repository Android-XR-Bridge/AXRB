package java.net;

import java.io.ByteArrayOutputStream;
import java.io.UnsupportedEncodingException;

public class URLDecoder {
    public URLDecoder() {}

    @Deprecated
    public static String decode(String s) {
        try {
            return decode(s, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            throw new IllegalStateException(e);
        }
    }

    public static String decode(String s, String enc) throws UnsupportedEncodingException {
        if (enc == null || enc.isEmpty()) throw new UnsupportedEncodingException("URLDecoder: empty string enc parameter");
        StringBuilder sb = new StringBuilder(s.length());
        ByteArrayOutputStream bytes = null;
        int i = 0;
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c == '+') {
                sb.append(' ');
                i++;
            } else if (c == '%') {
                if (bytes == null) bytes = new ByteArrayOutputStream();
                bytes.reset();
                while (i < s.length() && s.charAt(i) == '%') {
                    if (i + 2 >= s.length()) {
                        throw new IllegalArgumentException("URLDecoder: Incomplete trailing escape (%) pattern");
                    }
                    int v;
                    try {
                        v = Integer.parseInt(s.substring(i + 1, i + 3), 16);
                    } catch (NumberFormatException e) {
                        throw new IllegalArgumentException("URLDecoder: Illegal hex characters in escape (%) pattern");
                    }
                    bytes.write(v);
                    i += 3;
                }
                sb.append(new String(bytes.toByteArray(), enc));
            } else {
                sb.append(c);
                i++;
            }
        }
        return sb.toString();
    }
}
