package java.net;

import java.io.UnsupportedEncodingException;

public class URLEncoder {
    private URLEncoder() {}

    @Deprecated
    public static String encode(String s) {
        try {
            return encode(s, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            throw new IllegalStateException(e);
        }
    }

    public static String encode(String s, String enc) throws UnsupportedEncodingException {
        if (enc == null) throw new NullPointerException("charsetName");
        StringBuilder sb = new StringBuilder(s.length());
        byte[] bytes = s.getBytes(enc);
        for (byte b : bytes) {
            int c = b & 0xff;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'
                    || c == '*' || c == '_') {
                sb.append((char) c);
            } else if (c == ' ') {
                sb.append('+');
            } else {
                sb.append('%').append(Character.toUpperCase(Character.forDigit(c >> 4, 16)))
                        .append(Character.toUpperCase(Character.forDigit(c & 0xf, 16)));
            }
        }
        return sb.toString();
    }
}
