package android.view.animation;

import android.content.Context;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.Xml;
import java.io.IOException;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/** Loads view animations and interpolators from XML resources. */
public class AnimationUtils {
    public static long currentAnimationTimeMillis() {
        return SystemClock.uptimeMillis();
    }

    public static Animation loadAnimation(Context context, int id) throws Resources.NotFoundException {
        XmlResourceParser parser = null;
        try {
            parser = context.getResources().getAnimation(id);
            return createAnimationFromXml(context, parser, null, Xml.asAttributeSet(parser));
        } catch (XmlPullParserException | IOException ex) {
            throw new Resources.NotFoundException("Can't load animation resource ID #0x" + Integer.toHexString(id),
                    ex);
        } finally {
            if (parser != null) parser.close();
        }
    }

    private static Animation createAnimationFromXml(Context c, XmlPullParser parser, AnimationSet parent,
            AttributeSet attrs) throws XmlPullParserException, IOException {
        Animation anim = null;
        int type;
        int depth = parser.getDepth();
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth)
                && type != XmlPullParser.END_DOCUMENT) {
            if (type != XmlPullParser.START_TAG) continue;
            String name = parser.getName();
            if (name.equals("set")) {
                anim = new AnimationSet(c, attrs);
                createAnimationFromXml(c, parser, (AnimationSet) anim, attrs);
            } else if (name.equals("alpha")) {
                anim = new AlphaAnimation(c, attrs);
            } else if (name.equals("scale")) {
                anim = new ScaleAnimation(c, attrs);
            } else if (name.equals("rotate")) {
                anim = new RotateAnimation(c, attrs);
            } else if (name.equals("translate")) {
                anim = new TranslateAnimation(c, attrs);
            } else {
                throw new RuntimeException("Unknown animation name: " + parser.getName());
            }
            if (parent != null) parent.addAnimation(anim);
        }
        return anim;
    }

    public static Animation makeInAnimation(Context c, boolean fromLeft) {
        Animation a;
        if (fromLeft) {
            a = loadAnimation(c, android.R.anim.slide_in_left);
        } else {
            a = new TranslateAnimation(Animation.RELATIVE_TO_PARENT, 0.5f, Animation.ABSOLUTE, 0,
                    Animation.ABSOLUTE, 0, Animation.ABSOLUTE, 0);
            a.setDuration(c.getResources().getInteger(android.R.integer.config_mediumAnimTime));
        }
        a.setInterpolator(new DecelerateInterpolator());
        a.setStartTime(currentAnimationTimeMillis());
        return a;
    }

    public static Animation makeOutAnimation(Context c, boolean toRight) {
        Animation a;
        if (toRight) {
            a = loadAnimation(c, android.R.anim.slide_out_right);
        } else {
            a = new TranslateAnimation(Animation.ABSOLUTE, 0, Animation.RELATIVE_TO_PARENT, -0.5f,
                    Animation.ABSOLUTE, 0, Animation.ABSOLUTE, 0);
            a.setDuration(c.getResources().getInteger(android.R.integer.config_mediumAnimTime));
        }
        a.setInterpolator(new AccelerateInterpolator());
        a.setStartTime(currentAnimationTimeMillis());
        return a;
    }

    public static Interpolator loadInterpolator(Context context, int id) throws Resources.NotFoundException {
        XmlResourceParser parser = null;
        try {
            parser = context.getResources().getAnimation(id);
            return createInterpolatorFromXml(context, parser);
        } catch (XmlPullParserException | IOException ex) {
            throw new Resources.NotFoundException("Can't load interpolator resource ID #0x"
                    + Integer.toHexString(id), ex);
        } finally {
            if (parser != null) parser.close();
        }
    }

    private static Interpolator createInterpolatorFromXml(Context c, XmlPullParser parser)
            throws XmlPullParserException, IOException {
        Interpolator interpolator = null;
        int type;
        int depth = parser.getDepth();
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth)
                && type != XmlPullParser.END_DOCUMENT) {
            if (type != XmlPullParser.START_TAG) continue;
            AttributeSet attrs = Xml.asAttributeSet(parser);
            String name = parser.getName();
            if (name.equals("linearInterpolator")) {
                interpolator = new LinearInterpolator();
            } else if (name.equals("accelerateInterpolator")) {
                interpolator = new AccelerateInterpolator(c, attrs);
            } else if (name.equals("decelerateInterpolator")) {
                interpolator = new DecelerateInterpolator(c, attrs);
            } else if (name.equals("accelerateDecelerateInterpolator")) {
                interpolator = new AccelerateDecelerateInterpolator();
            } else if (name.equals("cycleInterpolator")) {
                interpolator = new CycleInterpolator(c, attrs);
            } else if (name.equals("anticipateInterpolator")) {
                interpolator = new AnticipateInterpolator(c, attrs);
            } else if (name.equals("overshootInterpolator")) {
                interpolator = new OvershootInterpolator(c, attrs);
            } else if (name.equals("anticipateOvershootInterpolator")) {
                interpolator = new AnticipateOvershootInterpolator(c, attrs);
            } else if (name.equals("bounceInterpolator")) {
                interpolator = new BounceInterpolator();
            } else if (name.equals("pathInterpolator")) {
                interpolator = new PathInterpolator(c, attrs);
            } else {
                throw new RuntimeException("Unknown interpolator name: " + parser.getName());
            }
        }
        return interpolator;
    }
}
