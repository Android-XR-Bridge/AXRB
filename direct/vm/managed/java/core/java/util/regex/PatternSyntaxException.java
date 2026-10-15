package java.util.regex;

public class PatternSyntaxException extends IllegalArgumentException {
    private final String desc;
    private final String pattern;
    private final int index;

    public PatternSyntaxException(String desc, String regex, int index) {
        this.desc = desc;
        this.pattern = regex;
        this.index = index;
    }

    public int getIndex() {
        return index;
    }

    public String getDescription() {
        return desc;
    }

    public String getPattern() {
        return pattern;
    }

    public String getMessage() {
        StringBuilder sb = new StringBuilder(desc);
        if (index >= 0) sb.append(" near index ").append(index);
        sb.append('\n').append(pattern);
        if (index >= 0 && pattern != null && index <= pattern.length()) {
            sb.append('\n');
            for (int i = 0; i < index; i++) sb.append(' ');
            sb.append('^');
        }
        return sb.toString();
    }
}
