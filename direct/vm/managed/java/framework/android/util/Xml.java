package android.util;

import org.xmlpull.v1.XmlPullParser;

/** XML utilities. */
public class Xml {
    public static final String FEATURE_RELAXED = "http://xmlpull.org/v1/doc/features.html#relaxed";

    private Xml() {}

    /** The attributes of the parser's current tag; compiled resource XML parsers are their own AttributeSet. */
    public static AttributeSet asAttributeSet(XmlPullParser parser) {
        if (parser instanceof AttributeSet) return (AttributeSet) parser;
        throw new UnsupportedOperationException("attribute sets of text XML parsers are not supported by this runtime");
    }
}
