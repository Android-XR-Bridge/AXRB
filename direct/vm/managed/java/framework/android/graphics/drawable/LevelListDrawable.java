package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import java.io.IOException;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Chooses a child drawable by level: XML <level-list>. */
public class LevelListDrawable extends DrawableContainer {
    private final ArrayList<int[]> mRanges = new ArrayList<int[]>();

    public void addLevel(int low, int high, Drawable drawable) {
        if (drawable != null) {
            mRanges.add(new int[] {low, high});
            addChild(drawable);
            onLevelChange(getLevel());
        }
    }

    @Override
    protected boolean onLevelChange(int level) {
        int idx = -1;
        for (int i = 0; i < mRanges.size(); i++) {
            if (level >= mRanges.get(i)[0] && level <= mRanges.get(i)[1]) {
                idx = i;
                break;
            }
        }
        return selectDrawable(idx) || super.onLevelChange(level);
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        super.inflate(r, parser, attrs, theme);
        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        int low = 0;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth || !parser.getName().equals("item")) continue;
            final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.LevelListDrawableItem);
            low = a.getInt(android.R.styleable.LevelListDrawableItem_minLevel, 0);
            int high = a.getInt(android.R.styleable.LevelListDrawableItem_maxLevel, 0);
            Drawable dr = a.getDrawable(android.R.styleable.LevelListDrawableItem_drawable);
            a.recycle();
            if (dr == null) dr = inflateChild(r, parser, attrs, theme);
            mRanges.add(new int[] {low, high});
            addChild(dr);
        }
        onLevelChange(getLevel());
    }
}