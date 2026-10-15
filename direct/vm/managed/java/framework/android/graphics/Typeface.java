package android.graphics;

/**
 * Font selection. Glyphs come from the host's UI font; the typeface carries
 * the family class and weight the host uses to pick a face.
 */
public class Typeface {
    public static final int NORMAL = 0;
    public static final int BOLD = 1;
    public static final int ITALIC = 2;
    public static final int BOLD_ITALIC = 3;

    static final int FAMILY_SANS = 0;
    static final int FAMILY_SERIF = 1;
    static final int FAMILY_MONO = 2;

    public static final Typeface DEFAULT = new Typeface(FAMILY_SANS, 400, NORMAL);
    public static final Typeface DEFAULT_BOLD = new Typeface(FAMILY_SANS, 700, BOLD);
    public static final Typeface SANS_SERIF = DEFAULT;
    public static final Typeface SERIF = new Typeface(FAMILY_SERIF, 400, NORMAL);
    public static final Typeface MONOSPACE = new Typeface(FAMILY_MONO, 400, NORMAL);

    final int family;
    final int weight;
    private final int style;

    Typeface(int family, int weight, int style) {
        this.family = family;
        this.weight = weight;
        this.style = style;
    }

    public int getStyle() {
        return style;
    }

    public int getWeight() {
        return weight;
    }

    public final boolean isBold() {
        return (style & BOLD) != 0;
    }

    public final boolean isItalic() {
        return (style & ITALIC) != 0;
    }

    public static Typeface create(String familyName, int style) {
        int family = FAMILY_SANS;
        int weight = 400;
        if (familyName != null) {
            if (familyName.equals("serif")) family = FAMILY_SERIF;
            else if (familyName.equals("monospace")) family = FAMILY_MONO;
            else if (familyName.equals("sans-serif-medium")) weight = 500;
            else if (familyName.equals("sans-serif-light")) weight = 300;
            else if (familyName.equals("sans-serif-thin")) weight = 100;
            else if (familyName.equals("sans-serif-black")) weight = 900;
        }
        if ((style & BOLD) != 0) weight = 700;
        return new Typeface(family, weight, style);
    }

    public static Typeface create(Typeface family, int style) {
        Typeface base = family == null ? DEFAULT : family;
        int weight = (style & BOLD) != 0 ? 700 : (base.weight == 700 ? 400 : base.weight);
        return new Typeface(base.family, weight, style);
    }

    public static Typeface defaultFromStyle(int style) {
        return create(DEFAULT, style);
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Typeface)) return false;
        Typeface t = (Typeface) o;
        return family == t.family && weight == t.weight && style == t.style;
    }

    @Override
    public int hashCode() {
        return family * 1000 + weight + style;
    }
}
