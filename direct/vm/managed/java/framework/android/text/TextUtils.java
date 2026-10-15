package android.text;

import java.util.Iterator;

public class TextUtils {
    private TextUtils() {}

    public static boolean isEmpty(CharSequence str) {
        return str == null || str.length() == 0;
    }

    public static boolean equals(CharSequence a, CharSequence b) {
        if (a == b) return true;
        if (a != null && b != null) {
            int length = a.length();
            if (length != b.length()) return false;
            if (a instanceof String && b instanceof String) return a.equals(b);
            for (int i = 0; i < length; i++) {
                if (a.charAt(i) != b.charAt(i)) return false;
            }
            return true;
        }
        return false;
    }

    public static String join(CharSequence delimiter, Object[] tokens) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < tokens.length; i++) {
            if (i > 0) sb.append(delimiter);
            sb.append(tokens[i]);
        }
        return sb.toString();
    }

    public static String join(CharSequence delimiter, Iterable tokens) {
        StringBuilder sb = new StringBuilder();
        Iterator it = tokens.iterator();
        boolean first = true;
        while (it.hasNext()) {
            if (!first) sb.append(delimiter);
            sb.append(it.next());
            first = false;
        }
        return sb.toString();
    }

    public static String[] split(String text, String expression) {
        if (text.length() == 0) return new String[0];
        return text.split(expression, -1);
    }

    public static int getTrimmedLength(CharSequence s) {
        int len = s.length();
        int start = 0;
        while (start < len && s.charAt(start) <= ' ') start++;
        int end = len;
        while (end > start && s.charAt(end - 1) <= ' ') end--;
        return end - start;
    }

    public static String htmlEncode(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '<': sb.append("&lt;"); break;
                case '>': sb.append("&gt;"); break;
                case '&': sb.append("&amp;"); break;
                case '\'': sb.append("&#39;"); break;
                case '"': sb.append("&quot;"); break;
                default: sb.append(c);
            }
        }
        return sb.toString();
    }

    public static boolean isDigitsOnly(CharSequence str) {
        for (int i = 0; i < str.length(); i++) {
            if (!Character.isDigit(str.charAt(i))) return false;
        }
        return true;
    }

    public static CharSequence concat(CharSequence... text) {
        StringBuilder sb = new StringBuilder();
        for (CharSequence t : text) sb.append(t);
        return sb.toString();
    }
}
