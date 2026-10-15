package android.graphics.drawable;

import android.content.res.Resources;
import android.util.AttributeSet;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** A launcher icon made of <background> and <foreground> layers (API 26), drawn stacked. */
public class AdaptiveIconDrawable extends LayerDrawable {
    AdaptiveIconDrawable() {}

    public AdaptiveIconDrawable(Drawable backgroundDrawable, Drawable foregroundDrawable) {
        if (backgroundDrawable != null) addLayer(backgroundDrawable);
        if (foregroundDrawable != null) addLayer(foregroundDrawable);
    }

    public Drawable getBackground() {
        return getNumberOfLayers() > 0 ? getDrawable(0) : null;
    }

    public Drawable getForeground() {
        return getNumberOfLayers() > 1 ? getDrawable(1) : null;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth) continue;
            String name = parser.getName();
            if (!name.equals("background") && !name.equals("foreground")) continue;
            final android.content.res.TypedArray a = obtainAttributes(r, theme, attrs,
                    android.R.styleable.AdaptiveIconDrawableLayer);
            Drawable dr = a.getDrawable(android.R.styleable.AdaptiveIconDrawableLayer_drawable);
            a.recycle();
            if (dr == null) dr = inflateChild(r, parser, attrs, theme);
            addLayer(dr);
        }
    }
}