package android.os;

/** Literal, prefix, suffix and simple-glob string patterns (intent filter paths). */
public class PatternMatcher implements Parcelable {
    public static final int PATTERN_LITERAL = 0;
    public static final int PATTERN_PREFIX = 1;
    public static final int PATTERN_SIMPLE_GLOB = 2;
    public static final int PATTERN_ADVANCED_GLOB = 3;
    public static final int PATTERN_SUFFIX = 4;

    private final String mPattern;
    private final int mType;

    public PatternMatcher(String pattern, int type) {
        mPattern = pattern;
        mType = type;
    }

    public final String getPath() {
        return mPattern;
    }

    public final int getType() {
        return mType;
    }

    public boolean match(String str) {
        if (str == null) return false;
        switch (mType) {
            case PATTERN_LITERAL: return mPattern.equals(str);
            case PATTERN_PREFIX: return str.startsWith(mPattern);
            case PATTERN_SUFFIX: return str.endsWith(mPattern);
            case PATTERN_SIMPLE_GLOB:
            case PATTERN_ADVANCED_GLOB: return matchGlob(mPattern, 0, str, 0);
            default: return false;
        }
    }

    /** '.' matches one character, ".*" any run, "x*" a run of x; '\' escapes. */
    private static boolean matchGlob(String p, int pi, String s, int si) {
        while (pi < p.length()) {
            char c = p.charAt(pi);
            boolean escaped = false;
            if (c == '\\' && pi + 1 < p.length()) {
                c = p.charAt(++pi);
                escaped = true;
            }
            boolean star = pi + 1 < p.length() && p.charAt(pi + 1) == '*';
            if (star) {
                int next = pi + 2;
                for (int k = si; k <= s.length(); k++) {
                    if (matchGlob(p, next, s, k)) return true;
                    if (k < s.length() && !(c == '.' && !escaped) && s.charAt(k) != c) break;
                }
                return false;
            }
            if (si >= s.length()) return false;
            if (!(c == '.' && !escaped) && s.charAt(si) != c) return false;
            pi++;
            si++;
        }
        return si == s.length();
    }

    public static final Parcelable.Creator<PatternMatcher> CREATOR = new Parcelable.Creator<PatternMatcher>() {
        public PatternMatcher createFromParcel(Parcel source) {
            return new PatternMatcher(source.readString(), source.readInt());
        }

        public PatternMatcher[] newArray(int size) {
            return new PatternMatcher[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(mPattern);
        dest.writeInt(mType);
    }

    @Override
    public String toString() {
        return "PatternMatcher{" + mType + ": " + mPattern + "}";
    }
}
