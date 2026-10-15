package android.graphics.drawable;

import android.content.res.Resources;
import android.content.res.TypedArray;
import android.os.SystemClock;
import android.util.AttributeSet;
import java.io.IOException;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Frame-by-frame animation: XML <animation-list>. */
public class AnimationDrawable extends DrawableContainer implements Runnable, Animatable {
    private final ArrayList<Integer> mDurations = new ArrayList<Integer>();
    private boolean mOneShot;
    private boolean mRunning;
    private int mCurFrame = -1;

    public AnimationDrawable() {}

    public void addFrame(Drawable frame, int duration) {
        mDurations.add(duration);
        addChild(frame);
        if (mCurFrame < 0) setFrame(0, true, false);
    }

    public int getNumberOfFrames() {
        return mChildren.size();
    }

    public Drawable getFrame(int index) {
        return mChildren.get(index);
    }

    public int getDuration(int i) {
        return mDurations.get(i);
    }

    public boolean isOneShot() {
        return mOneShot;
    }

    public void setOneShot(boolean oneShot) {
        mOneShot = oneShot;
    }

    @Override
    public void start() {
        if (!mRunning) {
            mRunning = true;
            setFrame(mCurFrame < 0 ? 0 : mCurFrame, false, mChildren.size() > 1 || !mOneShot);
        }
    }

    @Override
    public void stop() {
        mRunning = false;
        unscheduleSelf(this);
    }

    @Override
    public boolean isRunning() {
        return mRunning;
    }

    @Override
    public void run() {
        int next = mCurFrame + 1;
        if (next >= mChildren.size()) {
            if (mOneShot) {
                mRunning = false;
                return;
            }
            next = 0;
        }
        setFrame(next, false, true);
    }

    private void setFrame(int frame, boolean unschedule, boolean animate) {
        if (frame >= mChildren.size()) return;
        mCurFrame = frame;
        selectDrawable(frame);
        if (unschedule || animate) unscheduleSelf(this);
        if (animate && mRunning) scheduleSelf(this, SystemClock.uptimeMillis() + mDurations.get(frame));
    }

    @Override
    public void inflate(Resources r, XmlPullParser parser, AttributeSet attrs, Resources.Theme theme)
            throws XmlPullParserException, IOException {
        final TypedArray a = obtainAttributes(r, theme, attrs, android.R.styleable.AnimationDrawable);
        mOneShot = a.getBoolean(android.R.styleable.AnimationDrawable_oneshot, false);
        mVariablePadding = a.getBoolean(android.R.styleable.AnimationDrawable_variablePadding, false);
        a.recycle();
        final int innerDepth = parser.getDepth() + 1;
        int type;
        int depth;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && ((depth = parser.getDepth()) >= innerDepth || type != XmlPullParser.END_TAG)) {
            if (type != XmlPullParser.START_TAG || depth > innerDepth || !parser.getName().equals("item")) continue;
            final TypedArray ia = obtainAttributes(r, theme, attrs, android.R.styleable.AnimationDrawableItem);
            final int duration = ia.getInt(android.R.styleable.AnimationDrawableItem_duration, -1);
            if (duration < 0) {
                ia.recycle();
                throw new XmlPullParserException(parser.getPositionDescription()
                        + ": <item> tag requires a 'duration' attribute");
            }
            Drawable dr = ia.getDrawable(android.R.styleable.AnimationDrawableItem_drawable);
            ia.recycle();
            if (dr == null) dr = inflateChild(r, parser, attrs, theme);
            mDurations.add(duration);
            addChild(dr);
        }
        setFrame(0, true, false);
    }
}