package android.os;

import java.util.Arrays;
import java.util.Locale;

/** An ordered list of locales (the runtime has exactly one configured locale). */
public final class LocaleList {
    private final Locale[] mList;

    public LocaleList(Locale... list) {
        mList = list == null ? new Locale[0] : list.clone();
    }

    public static LocaleList getDefault() {
        return new LocaleList(Locale.getDefault());
    }

    public static LocaleList getAdjustedDefault() {
        return getDefault();
    }

    public static LocaleList getEmptyLocaleList() {
        return new LocaleList();
    }

    public static LocaleList forLanguageTags(String list) {
        if (list == null || list.isEmpty()) return getEmptyLocaleList();
        String[] tags = list.split(",");
        Locale[] locales = new Locale[tags.length];
        for (int i = 0; i < tags.length; i++) locales[i] = Locale.forLanguageTag(tags[i]);
        return new LocaleList(locales);
    }

    public Locale get(int index) {
        return index >= 0 && index < mList.length ? mList[index] : null;
    }

    public boolean isEmpty() {
        return mList.length == 0;
    }

    public int size() {
        return mList.length;
    }

    public int indexOf(Locale locale) {
        for (int i = 0; i < mList.length; i++) {
            if (mList[i].equals(locale)) return i;
        }
        return -1;
    }

    public String toLanguageTags() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < mList.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(mList[i].toLanguageTag());
        }
        return sb.toString();
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof LocaleList && Arrays.equals(mList, ((LocaleList) other).mList);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(mList);
    }

    @Override
    public String toString() {
        return "[" + toLanguageTags() + "]";
    }
}
