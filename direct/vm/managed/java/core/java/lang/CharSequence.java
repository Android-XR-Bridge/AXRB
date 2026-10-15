package java.lang;

public interface CharSequence {
    int length();

    char charAt(int index);

    CharSequence subSequence(int start, int end);

    String toString();

    default boolean isEmpty() {
        return length() == 0;
    }

    static int compare(CharSequence a, CharSequence b) {
        int n = Math.min(a.length(), b.length());
        for (int i = 0; i < n; i++) {
            char x = a.charAt(i), y = b.charAt(i);
            if (x != y) return x - y;
        }
        return a.length() - b.length();
    }
}
