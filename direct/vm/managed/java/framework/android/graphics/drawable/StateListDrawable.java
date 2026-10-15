package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import android.util.StateSet;
import java.io.IOException;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Chooses a child drawable by view state: XML <selector>. */
public class StateListDrawable extends DrawableContainer {
    private final ArrayList<int[]> mStateSets = new ArrayList<int[]>();

    public StateListDrawable() {}

    public void addState(int[] stateSet, Drawable drawable) {
        if (drawable != null) {
            mStateSets.add(stateSet);
            addChild(drawable);
            onStateChange(getState());
        }
    }

    public int getStateCount() {
        return mStateSets.size();
    }

    public int[] getStateSet(int index) {
        return mStateSets.get(index);
    }

    public Drawable getStateDrawable(int index) {
        return mChildren.get(index);
    }

    public int findStateDrawableIndex(int[] stateSet) {
        for (int i = 0; i < mStateSets.size(); i++) {
            if (StateSet.stateSetMatches(mStateSets.get(i), stateSet)) return i;
        }
        return -1;
    }

    @Override
    public boolean isStateful() {
        return true;
    }

    @Override
    protected boolean onStateChange(int[] stateSet) {
        final boolean changed = super.onStateChange(stateSet);
        int idx = findStateDrawableIndex(stateSet);
        if (idx < 0) idx = findStateDrawableIndex(StateSet.WILD_CARD);
        return selectDrawable(idx) || changed;
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.StateListDrawable);
        mVariablePadding = a.getBoolean(android.R.styleable.StateListDrawable_variablePadding, false);
        mConstantSize = a.getBoolean(android.R.styleable.StateListDrawable_constantSize, false);
        setVisible(a.getBoolean(android.R.styleable.StateListDrawable_visible, true), true);
        a.recycle();

        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth || !parser.getName().equals("item")) continue;
            final TypedArray ia = obtainAttributes(r, theme, attrs, android.R.styleable.StateListDrawableItem);
            Drawable dr = ia.getDrawable(android.R.styleable.StateListDrawableItem_drawable);
            ia.recycle();
            final int[] states = extractStateSet(attrs);
            if (dr == null) dr = inflateChild(r, parser, attrs, theme);
            mStateSets.add(states);
            addChild(dr);
        }
        onStateChange(getState());
    }

    /** The state attributes of an <item>: every attribute other than drawable/id. */
    static int[] extractStateSet(AttributeSet attrs) {
        int j = 0;
        final int numAttrs = attrs.getAttributeCount();
        int[] states = new int[numAttrs];
        for (int i = 0; i < numAttrs; i++) {
            final int stateResId = attrs.getAttributeNameResource(i);
            switch (stateResId) {
                case 0:
                    break;
                case android.R.attr.drawable:
                case android.R.attr.id:
                case android.R.attr.color:
                case android.R.attr.alpha:
                    continue;
                default:
                    states[j++] = attrs.getAttributeBooleanValue(i, false) ? stateResId : -stateResId;
            }
        }
        return StateSet.trimStateSet(states, j);
    }
}