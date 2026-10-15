package android.text;

public interface Editable extends CharSequence, Appendable {
    Editable replace(int st, int en, CharSequence source, int start, int end);

    Editable replace(int st, int en, CharSequence text);

    Editable insert(int where, CharSequence text);

    Editable delete(int st, int en);

    Editable append(CharSequence text);

    Editable append(CharSequence text, int start, int end);

    Editable append(char text);

    void clear();

    void setFilters(InputFilter[] filters);

    InputFilter[] getFilters();

    class Factory {
        private static final Factory sInstance = new Factory();

        public static Factory getInstance() {
            return sInstance;
        }

        public Editable newEditable(CharSequence source) {
            return new SpannableStringBuilder(source);
        }
    }
}
