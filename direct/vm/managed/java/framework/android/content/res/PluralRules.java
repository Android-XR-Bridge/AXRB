package android.content.res;

import java.util.Locale;

/**
 * CLDR cardinal plural categories for integers, for the languages this
 * runtime knows; every other language uses "one" for 1 and "other" otherwise.
 * Runtime-internal.
 */
final class PluralRules {
    private PluralRules() {}

    static int quantityKey(Locale locale, int n) {
        String lang = locale != null ? locale.getLanguage() : "en";
        int abs = Math.abs(n);
        int mod10 = abs % 10;
        int mod100 = abs % 100;
        switch (lang) {
            case "ja": case "ko": case "zh": case "th": case "vi": case "id": case "in": case "ms": case "my":
            case "lo": case "km":
                return Resources.ATTR_OTHER;
            case "fr": case "hy": case "kab":
                return abs == 0 || abs == 1 ? Resources.ATTR_ONE : Resources.ATTR_OTHER;
            case "pt":
                return abs == 0 || abs == 1 ? Resources.ATTR_ONE : Resources.ATTR_OTHER;
            case "ru": case "uk": case "be":
                if (mod10 == 1 && mod100 != 11) return Resources.ATTR_ONE;
                if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return Resources.ATTR_FEW;
                return Resources.ATTR_MANY;
            case "pl":
                if (abs == 1) return Resources.ATTR_ONE;
                if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return Resources.ATTR_FEW;
                return Resources.ATTR_MANY;
            case "cs": case "sk":
                if (abs == 1) return Resources.ATTR_ONE;
                if (abs >= 2 && abs <= 4) return Resources.ATTR_FEW;
                return Resources.ATTR_OTHER;
            case "ro": case "mo":
                if (abs == 1) return Resources.ATTR_ONE;
                if (abs == 0 || (mod100 >= 2 && mod100 <= 19)) return Resources.ATTR_FEW;
                return Resources.ATTR_OTHER;
            case "lt":
                if (mod10 == 1 && (mod100 < 11 || mod100 > 19)) return Resources.ATTR_ONE;
                if (mod10 >= 2 && (mod100 < 11 || mod100 > 19)) return Resources.ATTR_FEW;
                return Resources.ATTR_OTHER;
            case "lv":
                if (mod10 == 0 || (mod100 >= 11 && mod100 <= 19)) return Resources.ATTR_ZERO;
                if (mod10 == 1 && mod100 != 11) return Resources.ATTR_ONE;
                return Resources.ATTR_OTHER;
            case "ar":
                if (abs == 0) return Resources.ATTR_ZERO;
                if (abs == 1) return Resources.ATTR_ONE;
                if (abs == 2) return Resources.ATTR_TWO;
                if (mod100 >= 3 && mod100 <= 10) return Resources.ATTR_FEW;
                if (mod100 >= 11) return Resources.ATTR_MANY;
                return Resources.ATTR_OTHER;
            case "he": case "iw":
                if (abs == 1) return Resources.ATTR_ONE;
                if (abs == 2) return Resources.ATTR_TWO;
                return Resources.ATTR_OTHER;
            case "sl":
                if (mod100 == 1) return Resources.ATTR_ONE;
                if (mod100 == 2) return Resources.ATTR_TWO;
                if (mod100 == 3 || mod100 == 4) return Resources.ATTR_FEW;
                return Resources.ATTR_OTHER;
            case "hr": case "sr": case "bs":
                if (mod10 == 1 && mod100 != 11) return Resources.ATTR_ONE;
                if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return Resources.ATTR_FEW;
                return Resources.ATTR_OTHER;
            case "ga":
                if (abs == 1) return Resources.ATTR_ONE;
                if (abs == 2) return Resources.ATTR_TWO;
                if (abs >= 3 && abs <= 6) return Resources.ATTR_FEW;
                if (abs >= 7 && abs <= 10) return Resources.ATTR_MANY;
                return Resources.ATTR_OTHER;
            default:
                return abs == 1 ? Resources.ATTR_ONE : Resources.ATTR_OTHER;
        }
    }

    static String keyName(int key) {
        switch (key) {
            case Resources.ATTR_ZERO: return "zero";
            case Resources.ATTR_ONE: return "one";
            case Resources.ATTR_TWO: return "two";
            case Resources.ATTR_FEW: return "few";
            case Resources.ATTR_MANY: return "many";
            default: return "other";
        }
    }
}
