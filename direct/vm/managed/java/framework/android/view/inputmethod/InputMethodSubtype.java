package android.view.inputmethod;

public final class InputMethodSubtype {
    private final String mLocale;
    private final String mMode;
    private final String mExtraValue;

    public InputMethodSubtype(String locale, String mode, String extraValue) {
        mLocale = locale;
        mMode = mode;
        mExtraValue = extraValue;
    }

    public String getLocale() {
        return mLocale;
    }

    public String getLanguageTag() {
        return mLocale.replace('_', '-');
    }

    public String getMode() {
        return mMode;
    }

    public String getExtraValue() {
        return mExtraValue;
    }

    public boolean isAuxiliary() {
        return false;
    }
}
