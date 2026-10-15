package android.graphics;

import java.util.Locale;

/** Drawing style. Text measurement comes from the host's DirectWrite fonts. */
public class Paint {
    public static final int ANTI_ALIAS_FLAG = 0x01;
    public static final int FILTER_BITMAP_FLAG = 0x02;
    public static final int DITHER_FLAG = 0x04;
    public static final int UNDERLINE_TEXT_FLAG = 0x08;
    public static final int STRIKE_THRU_TEXT_FLAG = 0x10;
    public static final int FAKE_BOLD_TEXT_FLAG = 0x20;
    public static final int LINEAR_TEXT_FLAG = 0x40;
    public static final int SUBPIXEL_TEXT_FLAG = 0x80;
    public static final int EMBEDDED_BITMAP_TEXT_FLAG = 0x400;

    public static final int HINTING_OFF = 0x0;
    public static final int HINTING_ON = 0x1;

    public enum Style {
        FILL, STROKE, FILL_AND_STROKE
    }

    public enum Cap {
        BUTT, ROUND, SQUARE
    }

    public enum Join {
        MITER, ROUND, BEVEL
    }

    public enum Align {
        LEFT, CENTER, RIGHT
    }

    public static class FontMetrics {
        public float top;
        public float ascent;
        public float descent;
        public float bottom;
        public float leading;
    }

    public static class FontMetricsInt {
        public int top;
        public int ascent;
        public int descent;
        public int bottom;
        public int leading;

        @Override
        public String toString() {
            return "FontMetricsInt: top=" + top + " ascent=" + ascent + " descent=" + descent + " bottom=" + bottom
                    + " leading=" + leading;
        }
    }

    private int flags;
    private int color = Color.BLACK;
    private float textSize = 12f;
    private float textScaleX = 1f;
    private float textSkewX;
    private float letterSpacing;
    private float strokeWidth;
    private float strokeMiter = 4f;
    private Style style = Style.FILL;
    private Cap cap = Cap.BUTT;
    private Join join = Join.MITER;
    private Align textAlign = Align.LEFT;
    private Typeface typeface;
    private Shader shader;
    private ColorFilter colorFilter;
    private Xfermode xfermode;
    private Locale textLocale = Locale.getDefault();
    private float shadowRadius, shadowDx, shadowDy;
    private int shadowColor;

    public Paint() {
        this(0);
    }

    public Paint(int flags) {
        this.flags = flags | EMBEDDED_BITMAP_TEXT_FLAG;
    }

    public Paint(Paint paint) {
        set(paint);
    }

    public void set(Paint src) {
        if (src == this) return;
        flags = src.flags;
        color = src.color;
        textSize = src.textSize;
        textScaleX = src.textScaleX;
        textSkewX = src.textSkewX;
        letterSpacing = src.letterSpacing;
        strokeWidth = src.strokeWidth;
        strokeMiter = src.strokeMiter;
        style = src.style;
        cap = src.cap;
        join = src.join;
        textAlign = src.textAlign;
        typeface = src.typeface;
        shader = src.shader;
        colorFilter = src.colorFilter;
        xfermode = src.xfermode;
        textLocale = src.textLocale;
        shadowRadius = src.shadowRadius;
        shadowDx = src.shadowDx;
        shadowDy = src.shadowDy;
        shadowColor = src.shadowColor;
    }

    public void reset() {
        set(new Paint());
    }

    public int getFlags() {
        return flags;
    }

    public void setFlags(int flags) {
        this.flags = flags;
    }

    private void setFlag(int flag, boolean on) {
        flags = on ? flags | flag : flags & ~flag;
    }

    public int getHinting() {
        return HINTING_ON;
    }

    public void setHinting(int mode) {}

    public final boolean isAntiAlias() {
        return (flags & ANTI_ALIAS_FLAG) != 0;
    }

    public void setAntiAlias(boolean aa) {
        setFlag(ANTI_ALIAS_FLAG, aa);
    }

    public final boolean isDither() {
        return (flags & DITHER_FLAG) != 0;
    }

    public void setDither(boolean dither) {
        setFlag(DITHER_FLAG, dither);
    }

    public final boolean isFilterBitmap() {
        return (flags & FILTER_BITMAP_FLAG) != 0;
    }

    public void setFilterBitmap(boolean filter) {
        setFlag(FILTER_BITMAP_FLAG, filter);
    }

    public final boolean isUnderlineText() {
        return (flags & UNDERLINE_TEXT_FLAG) != 0;
    }

    public void setUnderlineText(boolean underlineText) {
        setFlag(UNDERLINE_TEXT_FLAG, underlineText);
    }

    public final boolean isStrikeThruText() {
        return (flags & STRIKE_THRU_TEXT_FLAG) != 0;
    }

    public void setStrikeThruText(boolean strikeThruText) {
        setFlag(STRIKE_THRU_TEXT_FLAG, strikeThruText);
    }

    public final boolean isFakeBoldText() {
        return (flags & FAKE_BOLD_TEXT_FLAG) != 0;
    }

    public void setFakeBoldText(boolean fakeBoldText) {
        setFlag(FAKE_BOLD_TEXT_FLAG, fakeBoldText);
    }

    public final boolean isLinearText() {
        return (flags & LINEAR_TEXT_FLAG) != 0;
    }

    public void setLinearText(boolean linearText) {
        setFlag(LINEAR_TEXT_FLAG, linearText);
    }

    public final boolean isSubpixelText() {
        return (flags & SUBPIXEL_TEXT_FLAG) != 0;
    }

    public void setSubpixelText(boolean subpixelText) {
        setFlag(SUBPIXEL_TEXT_FLAG, subpixelText);
    }

    public void setElegantTextHeight(boolean elegant) {}

    public int getColor() {
        return color;
    }

    public void setColor(int color) {
        this.color = color;
    }

    public int getAlpha() {
        return color >>> 24;
    }

    public void setAlpha(int a) {
        color = (color & 0x00FFFFFF) | ((a & 0xFF) << 24);
    }

    public void setARGB(int a, int r, int g, int b) {
        setColor((a << 24) | (r << 16) | (g << 8) | b);
    }

    public Style getStyle() {
        return style;
    }

    public void setStyle(Style style) {
        this.style = style;
    }

    public float getStrokeWidth() {
        return strokeWidth;
    }

    public void setStrokeWidth(float width) {
        if (width >= 0) strokeWidth = width;
    }

    public float getStrokeMiter() {
        return strokeMiter;
    }

    public void setStrokeMiter(float miter) {
        if (miter >= 0) strokeMiter = miter;
    }

    public Cap getStrokeCap() {
        return cap;
    }

    public void setStrokeCap(Cap cap) {
        this.cap = cap;
    }

    public Join getStrokeJoin() {
        return join;
    }

    public void setStrokeJoin(Join join) {
        this.join = join;
    }

    public Shader getShader() {
        return shader;
    }

    public Shader setShader(Shader shader) {
        this.shader = shader;
        return shader;
    }

    public ColorFilter getColorFilter() {
        return colorFilter;
    }

    public ColorFilter setColorFilter(ColorFilter filter) {
        colorFilter = filter;
        return filter;
    }

    public Xfermode getXfermode() {
        return xfermode;
    }

    public Xfermode setXfermode(Xfermode xfermode) {
        this.xfermode = xfermode;
        return xfermode;
    }

    public Object setPathEffect(Object effect) {
        return effect;
    }

    public Object setMaskFilter(Object maskfilter) {
        return maskfilter;
    }

    public void setShadowLayer(float radius, float dx, float dy, int shadowColor) {
        shadowRadius = radius;
        shadowDx = dx;
        shadowDy = dy;
        this.shadowColor = shadowColor;
    }

    public void clearShadowLayer() {
        setShadowLayer(0, 0, 0, 0);
    }

    public float getShadowLayerRadius() {
        return shadowRadius;
    }

    public float getTextSize() {
        return textSize;
    }

    public void setTextSize(float textSize) {
        if (textSize > 0) this.textSize = textSize;
    }

    public float getTextScaleX() {
        return textScaleX;
    }

    public void setTextScaleX(float scaleX) {
        textScaleX = scaleX;
    }

    public float getTextSkewX() {
        return textSkewX;
    }

    public void setTextSkewX(float skewX) {
        textSkewX = skewX;
    }

    public float getLetterSpacing() {
        return letterSpacing;
    }

    public void setLetterSpacing(float letterSpacing) {
        this.letterSpacing = letterSpacing;
    }

    public Locale getTextLocale() {
        return textLocale;
    }

    public void setTextLocale(Locale locale) {
        if (locale == null) throw new IllegalArgumentException("locale cannot be null");
        textLocale = locale;
    }

    public Align getTextAlign() {
        return textAlign;
    }

    public void setTextAlign(Align align) {
        textAlign = align;
    }

    public Typeface getTypeface() {
        return typeface;
    }

    public Typeface setTypeface(Typeface typeface) {
        this.typeface = typeface;
        return typeface;
    }

    // --- host packing -----------------------------------------------------------

    /** Host font weight (CSS scale) this paint renders with. */
    final int hostWeight() {
        int w = typeface == null ? 400 : typeface.weight;
        return isFakeBoldText() ? Math.max(w, 700) : w;
    }

    final int hostFamily() {
        return typeface == null ? Typeface.FAMILY_SANS : typeface.family;
    }

    final boolean hostItalic() {
        return typeface != null && typeface.isItalic();
    }

    /** The color after the color filter. */
    final int hostColor() {
        return colorFilter != null ? colorFilter.filter(color) : color;
    }

    final int hostFlags() {
        int f = 0;
        if (isAntiAlias()) f |= 0x1;
        f |= style.ordinal() << 8;
        f |= cap.ordinal() << 10;
        f |= join.ordinal() << 12;
        if (isUnderlineText()) f |= 0x10000;
        if (isStrikeThruText()) f |= 0x20000;
        return f;
    }

    final float[] hostShader() {
        if (shader == null) return null;
        float[] spec = shader.nativeSpec();
        if (spec == null) return null;
        if (colorFilter != null) {
            int n = (int) spec[2];
            int c = spec.length - 2 * n;
            for (int i = 0; i < n; i++) {
                spec[c + i] = Float.intBitsToFloat(colorFilter.filter(Float.floatToRawIntBits(spec[c + i])));
            }
        }
        return spec;
    }

    // --- text measurement -----------------------------------------------------------

    public float measureText(String text) {
        if (text == null) throw new IllegalArgumentException("text cannot be null");
        if (text.length() == 0) return 0f;
        float w = nativeMeasureText(text, textSize, hostWeight(), hostFamily(), hostItalic());
        if (letterSpacing != 0) w += letterSpacing * textSize * text.length();
        return w * textScaleX;
    }

    public float measureText(String text, int start, int end) {
        if (text == null) throw new IllegalArgumentException("text cannot be null");
        if ((start | end | (end - start) | (text.length() - end)) < 0) throw new IndexOutOfBoundsException();
        return measureText(text.substring(start, end));
    }

    public float measureText(CharSequence text, int start, int end) {
        if (text == null) throw new IllegalArgumentException("text cannot be null");
        if ((start | end | (end - start) | (text.length() - end)) < 0) throw new IndexOutOfBoundsException();
        return measureText(text.subSequence(start, end).toString());
    }

    public float measureText(char[] text, int index, int count) {
        if (text == null) throw new IllegalArgumentException("text cannot be null");
        if ((index | count) < 0 || index + count > text.length) throw new ArrayIndexOutOfBoundsException();
        return measureText(new String(text, index, count));
    }

    /** Hidden: the advance of every char of `text` (0 inside a cluster). */
    public void getTextWidthsInternal(String text, float[] widths) {
        if (text.length() == 0) return;
        nativeGetTextWidths(text, textSize, hostWeight(), hostFamily(), hostItalic(), widths);
        if (letterSpacing != 0 || textScaleX != 1) {
            for (int i = 0; i < text.length(); i++) {
                widths[i] = (widths[i] + (widths[i] != 0 ? letterSpacing * textSize : 0)) * textScaleX;
            }
        }
    }

    public int getTextWidths(String text, int start, int end, float[] widths) {
        if ((start | end | (end - start) | (text.length() - end)) < 0) throw new IndexOutOfBoundsException();
        if (end - start > widths.length) throw new ArrayIndexOutOfBoundsException();
        String s = text.substring(start, end);
        float[] w = new float[s.length()];
        getTextWidthsInternal(s, w);
        System.arraycopy(w, 0, widths, 0, w.length);
        return s.length();
    }

    public int getTextWidths(String text, float[] widths) {
        return getTextWidths(text, 0, text.length(), widths);
    }

    public int getTextWidths(CharSequence text, int start, int end, float[] widths) {
        return getTextWidths(text.toString(), start, end, widths);
    }

    public int getTextWidths(char[] text, int index, int count, float[] widths) {
        return getTextWidths(new String(text, index, count), 0, count, widths);
    }

    public int breakText(String text, boolean measureForwards, float maxWidth, float[] measuredWidth) {
        float[] w = new float[text.length()];
        getTextWidthsInternal(text, w);
        float sum = 0;
        int count = 0;
        if (measureForwards) {
            for (int i = 0; i < w.length; i++) {
                if (sum + w[i] > maxWidth) break;
                sum += w[i];
                count++;
            }
        } else {
            for (int i = w.length - 1; i >= 0; i--) {
                if (sum + w[i] > maxWidth) break;
                sum += w[i];
                count++;
            }
        }
        if (measuredWidth != null) measuredWidth[0] = sum;
        return count;
    }

    public int breakText(CharSequence text, int start, int end, boolean measureForwards, float maxWidth,
            float[] measuredWidth) {
        return breakText(text.subSequence(start, end).toString(), measureForwards, maxWidth, measuredWidth);
    }

    public int breakText(char[] text, int index, int count, float maxWidth, float[] measuredWidth) {
        return breakText(new String(text, index, count), true, maxWidth, measuredWidth);
    }

    public float getFontMetrics(FontMetrics metrics) {
        float[] m = new float[5];
        nativeFontMetrics(textSize, hostWeight(), hostFamily(), hostItalic(), m);
        if (metrics != null) {
            metrics.top = m[0];
            metrics.ascent = m[1];
            metrics.descent = m[2];
            metrics.bottom = m[3];
            metrics.leading = m[4];
        }
        return m[2] - m[1] + m[4];
    }

    public FontMetrics getFontMetrics() {
        FontMetrics fm = new FontMetrics();
        getFontMetrics(fm);
        return fm;
    }

    public int getFontMetricsInt(FontMetricsInt fmi) {
        FontMetrics fm = new FontMetrics();
        getFontMetrics(fm);
        if (fmi != null) {
            fmi.top = (int) Math.floor(fm.top);
            fmi.ascent = Math.round(fm.ascent);
            fmi.descent = Math.round(fm.descent);
            fmi.bottom = (int) Math.ceil(fm.bottom);
            fmi.leading = Math.round(fm.leading);
        }
        return Math.round(fm.descent) - Math.round(fm.ascent) + Math.round(fm.leading);
    }

    public FontMetricsInt getFontMetricsInt() {
        FontMetricsInt fmi = new FontMetricsInt();
        getFontMetricsInt(fmi);
        return fmi;
    }

    public float ascent() {
        return getFontMetrics().ascent;
    }

    public float descent() {
        return getFontMetrics().descent;
    }

    public float getFontSpacing() {
        return getFontMetrics(null);
    }

    public void getTextBounds(String text, int start, int end, Rect bounds) {
        FontMetrics fm = getFontMetrics();
        bounds.set(0, Math.round(fm.ascent), (int) Math.ceil(measureText(text, start, end)), Math.round(fm.descent));
    }

    public void getTextBounds(CharSequence text, int start, int end, Rect bounds) {
        getTextBounds(text.toString(), start, end, bounds);
    }

    public void getTextBounds(char[] text, int index, int count, Rect bounds) {
        getTextBounds(new String(text, index, count), 0, count, bounds);
    }

    public boolean hasGlyph(String string) {
        return string != null && string.length() > 0;
    }

    /** Width in pixels of `text` rendered at `size` px. */
    static native float nativeMeasureText(String text, float size, int weight, int family, boolean italic);

    static native void nativeGetTextWidths(String text, float size, int weight, int family, boolean italic,
            float[] out);

    /** out = {top, ascent, descent, bottom, leading}; ascent and top are negative. */
    static native void nativeFontMetrics(float size, int weight, int family, boolean italic, float[] out);
}
