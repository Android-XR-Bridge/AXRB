package android.text;

/** An editable character sequence (spans are not tracked). */
public class SpannableStringBuilder implements Editable, Spanned {
    private final StringBuilder mText;
    private InputFilter[] mFilters = new InputFilter[0];

    public SpannableStringBuilder() {
        this("");
    }

    public SpannableStringBuilder(CharSequence text) {
        mText = new StringBuilder(text != null ? text : "");
    }

    public SpannableStringBuilder(CharSequence text, int start, int end) {
        mText = new StringBuilder(text.subSequence(start, end));
    }

    public Editable replace(int st, int en, CharSequence source, int start, int end) {
        CharSequence repl = source.subSequence(start, end);
        for (InputFilter f : mFilters) {
            CharSequence r = f.filter(repl, 0, repl.length(), this, st, en);
            if (r != null) repl = r;
        }
        mText.replace(st, en, repl.toString());
        return this;
    }

    public Editable replace(int st, int en, CharSequence text) {
        return replace(st, en, text, 0, text.length());
    }

    public Editable insert(int where, CharSequence text) {
        return replace(where, where, text);
    }

    public Editable delete(int st, int en) {
        mText.delete(st, en);
        return this;
    }

    public Editable append(CharSequence text) {
        return replace(mText.length(), mText.length(), text);
    }

    public Editable append(CharSequence text, int start, int end) {
        return replace(mText.length(), mText.length(), text, start, end);
    }

    public Editable append(char text) {
        return append(String.valueOf(text));
    }

    public void clear() {
        mText.setLength(0);
    }

    public void setFilters(InputFilter[] filters) {
        mFilters = filters != null ? filters : new InputFilter[0];
    }

    public InputFilter[] getFilters() {
        return mFilters;
    }

    public int length() {
        return mText.length();
    }

    public char charAt(int index) {
        return mText.charAt(index);
    }

    public CharSequence subSequence(int start, int end) {
        return mText.substring(start, end);
    }

    public String toString() {
        return mText.toString();
    }
}
