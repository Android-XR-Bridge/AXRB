package android.content.res;

import android.graphics.Color;
import android.util.AttributeSet;
import android.util.StateSet;
import android.util.TypedValue;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Colors keyed by view state sets, from a res/color <selector> or a single color. */
public class ColorStateList {
    private static final int[][] EMPTY = new int[][] {new int[0]};

    private int[][] mStateSpecs;
    private int[] mColors;
    private int mDefaultColor;
    private boolean mIsOpaque;

    private ColorStateList() {}

    public ColorStateList(int[][] states, int[] colors) {
        mStateSpecs = states;
        mColors = colors;
        onColorsChanged();
    }

    public static ColorStateList valueOf(int color) {
        return new ColorStateList(EMPTY, new int[] {color});
    }

    public static ColorStateList createFromXml(Resources r, XmlPullParser parser)
            throws XmlPullParserException, IOException {
        return createFromXml(r, parser, null);
    }

    public static ColorStateList createFromXml(Resources r, XmlPullParser parser, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        int type;
        while ((type = parser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
            // skip to the root
        }
        if (type != XmlPullParser.START_TAG) throw new XmlPullParserException("No start tag found");
        return createFromXmlInner(r, parser, (AttributeSet) parser, theme);
    }

    static ColorStateList createFromXmlInner(Resources r, XmlPullParser parser, AttributeSet attrs,
            Resources.Theme theme) throws XmlPullParserException, IOException {
        final String name = parser.getName();
        if (!"selector".equals(name)) {
            throw new XmlPullParserException(parser.getPositionDescription() + ": invalid color state list tag "
                    + name);
        }
        ColorStateList csl = new ColorStateList();
        csl.inflate(r, parser, attrs, theme);
        return csl;
    }

    private void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final int innerDepth = parser.getDepth() + 1;
        int depth;
        int type;
        ArrayList<int[]> specs = new ArrayList<int[]>();
        ArrayList<Integer> colors = new ArrayList<Integer>();
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth || !"item".equals(parser.getName())) continue;
            final TypedArray a = theme != null
                    ? theme.obtainStyledAttributes(attrs, android.R.styleable.ColorStateListItem, 0, 0)
                    : r.obtainAttributes(attrs, android.R.styleable.ColorStateListItem);
            int baseColor = Color.MAGENTA;
            if (a.getType(android.R.styleable.ColorStateListItem_color) != TypedValue.TYPE_ATTRIBUTE) {
                baseColor = a.getColor(android.R.styleable.ColorStateListItem_color, Color.MAGENTA);
            }
            float alphaMod = 1.0f;
            if (a.getType(android.R.styleable.ColorStateListItem_alpha) != TypedValue.TYPE_ATTRIBUTE) {
                alphaMod = a.getFloat(android.R.styleable.ColorStateListItem_alpha, 1.0f);
            }
            a.recycle();

            final int numAttrs = attrs.getAttributeCount();
            int[] stateSpec = new int[numAttrs];
            int j = 0;
            for (int i = 0; i < numAttrs; i++) {
                final int stateResId = attrs.getAttributeNameResource(i);
                if (stateResId == 0 || stateResId == android.R.attr.color || stateResId == android.R.attr.alpha) {
                    continue;
                }
                stateSpec[j++] = attrs.getAttributeBooleanValue(i, false) ? stateResId : -stateResId;
            }
            stateSpec = StateSet.trimStateSet(stateSpec, j);
            final int color = modulateColorAlpha(baseColor, alphaMod);
            colors.add(color);
            specs.add(stateSpec);
        }
        mColors = new int[colors.size()];
        mStateSpecs = new int[specs.size()][];
        for (int i = 0; i < mColors.length; i++) {
            mColors[i] = colors.get(i);
            mStateSpecs[i] = specs.get(i);
        }
        onColorsChanged();
    }

    private static int modulateColorAlpha(int baseColor, float alphaMod) {
        if (alphaMod == 1.0f) return baseColor;
        final int baseAlpha = Color.alpha(baseColor);
        final int alpha = Math.max(0, Math.min(255, (int) (baseAlpha * alphaMod + 0.5f)));
        return (baseColor & 0xFFFFFF) | (alpha << 24);
    }

    private void onColorsChanged() {
        int defaultColor = Color.RED;
        boolean isOpaque = true;
        final int[][] states = mStateSpecs;
        final int[] colors = mColors;
        final int n = states.length;
        if (n > 0) {
            defaultColor = colors[0];
            for (int i = n - 1; i > 0; i--) {
                if (states[i].length == 0) {
                    defaultColor = colors[i];
                    break;
                }
            }
            for (int i = 0; i < n; i++) {
                if (Color.alpha(colors[i]) != 0xFF) {
                    isOpaque = false;
                    break;
                }
            }
        }
        mDefaultColor = defaultColor;
        mIsOpaque = isOpaque;
    }

    public ColorStateList withAlpha(int alpha) {
        final int[] colors = new int[mColors.length];
        for (int i = 0; i < colors.length; i++) colors[i] = (mColors[i] & 0xFFFFFF) | (alpha << 24);
        return new ColorStateList(mStateSpecs, colors);
    }

    public boolean isStateful() {
        return mStateSpecs.length >= 1 && mStateSpecs[0].length > 0;
    }

    public boolean isOpaque() {
        return mIsOpaque;
    }

    public int getColorForState(int[] stateSet, int defaultColor) {
        final int setLength = mStateSpecs.length;
        for (int i = 0; i < setLength; i++) {
            final int[] stateSpec = mStateSpecs[i];
            if (StateSet.stateSetMatches(stateSpec, stateSet)) return mColors[i];
        }
        return defaultColor;
    }

    public int getDefaultColor() {
        return mDefaultColor;
    }

    public int[][] getStates() {
        return mStateSpecs;
    }

    public int[] getColors() {
        return mColors;
    }

    public int getChangingConfigurations() {
        return 0;
    }

    @Override
    public String toString() {
        return "ColorStateList{mStateSpecs=" + Arrays.deepToString(mStateSpecs) + "mColors="
                + Arrays.toString(mColors) + "mDefaultColor=" + mDefaultColor + '}';
    }
}
