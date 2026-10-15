package java.util;

/**
 * Language/country/variant identifiers. There is no locale data (no ICU): the
 * only locale-sensitive behaviour is the decimal and grouping separators the
 * Formatter uses, from a small built-in table (see numberSeparators). The
 * default locale is en_US unless the runtime sets another.
 */
public final class Locale implements Cloneable, java.io.Serializable {
    public static final Locale ROOT = new Locale("", "", "");
    public static final Locale ENGLISH = new Locale("en", "", "");
    public static final Locale US = new Locale("en", "US", "");
    public static final Locale UK = new Locale("en", "GB", "");
    public static final Locale CANADA = new Locale("en", "CA", "");
    public static final Locale GERMAN = new Locale("de", "", "");
    public static final Locale GERMANY = new Locale("de", "DE", "");
    public static final Locale FRENCH = new Locale("fr", "", "");
    public static final Locale FRANCE = new Locale("fr", "FR", "");
    public static final Locale ITALIAN = new Locale("it", "", "");
    public static final Locale ITALY = new Locale("it", "IT", "");
    public static final Locale JAPANESE = new Locale("ja", "", "");
    public static final Locale JAPAN = new Locale("ja", "JP", "");
    public static final Locale KOREAN = new Locale("ko", "", "");
    public static final Locale KOREA = new Locale("ko", "KR", "");
    public static final Locale CHINESE = new Locale("zh", "", "");
    public static final Locale CHINA = new Locale("zh", "CN", "");
    public static final Locale SIMPLIFIED_CHINESE = CHINA;
    public static final Locale TRADITIONAL_CHINESE = new Locale("zh", "TW", "");

    private static volatile Locale defaultLocale = initialDefault();

    /** From the user.language / user.region properties the runtime sets. */
    private static Locale initialDefault() {
        String language = System.getProperty("user.language");
        String region = System.getProperty("user.region");
        if (language == null || language.isEmpty()) return US;
        return new Locale(language, region != null ? region : "", "");
    }

    private final String language;
    private final String country;
    private final String variant;

    public Locale(String language, String country, String variant) {
        if (language == null || country == null || variant == null) throw new NullPointerException();
        this.language = convertOldIsoCodes(language.toLowerCase(ROOT_SAFE));
        this.country = country.toUpperCase(ROOT_SAFE);
        this.variant = variant;
    }

    public Locale(String language, String country) {
        this(language, country, "");
    }

    public Locale(String language) {
        this(language, "", "");
    }

    // String.toLowerCase(Locale) ignores its argument in this core library, so
    // null is safe here and avoids a class-initialisation cycle.
    private static final Locale ROOT_SAFE = null;

    private static String convertOldIsoCodes(String language) {
        if (language.equals("he")) return "iw";
        if (language.equals("yi")) return "ji";
        if (language.equals("id")) return "in";
        return language;
    }

    public static Locale getDefault() {
        return defaultLocale;
    }

    public static synchronized void setDefault(Locale newLocale) {
        if (newLocale == null) throw new NullPointerException("Can't set default locale to NULL");
        defaultLocale = newLocale;
    }

    public static Locale forLanguageTag(String languageTag) {
        String[] parts = languageTag.replace('_', '-').split("-");
        String lang = parts.length > 0 ? parts[0] : "";
        String country = "";
        for (int i = 1; i < parts.length; i++) {
            String p = parts[i];
            if (p.length() == 2 || (p.length() == 3 && Character.isDigit(p.charAt(0)))) {
                country = p;
                break;
            }
        }
        if (lang.equals("und")) lang = "";
        return new Locale(lang, country);
    }

    public String getLanguage() {
        return language;
    }

    public String getCountry() {
        return country;
    }

    public String getVariant() {
        return variant;
    }

    public String toLanguageTag() {
        String lang = language.isEmpty() ? "und" : language;
        if (lang.equals("iw")) lang = "he";
        if (lang.equals("ji")) lang = "yi";
        if (lang.equals("in")) lang = "id";
        return country.isEmpty() ? lang : lang + "-" + country;
    }

    /**
     * {decimal separator, grouping separator}. Covers the languages whose
     * conventions differ from English; anything not listed uses '.' and ','.
     */
    char[] numberSeparators() {
        switch (language) {
            case "de": case "es": case "it": case "nl": case "pt": case "in": case "da": case "tr":
            case "el": case "ro": case "hr": case "sl": case "sr": case "vi":
                return new char[] {',', '.'};
            case "fr":
                return new char[] {',', '\u202f'};
            case "ru": case "uk": case "pl": case "cs": case "sk": case "fi": case "sv": case "nb": case "hu":
            case "bg": case "lt": case "lv": case "et":
                return new char[] {',', '\u00a0'};
            default:
                return new char[] {'.', ','};
        }
    }

    @Override
    public String toString() {
        if (variant.isEmpty()) return country.isEmpty() ? language : language + "_" + country;
        return language + "_" + country + "_" + variant;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Locale)) return false;
        Locale l = (Locale) o;
        return language.equals(l.language) && country.equals(l.country) && variant.equals(l.variant);
    }

    @Override
    public int hashCode() {
        return (language.hashCode() * 31 + country.hashCode()) * 31 + variant.hashCode();
    }

    @Override
    public Object clone() {
        return this;
    }
}
