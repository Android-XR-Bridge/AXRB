package android.content.res;

import android.util.AttributeSet;
import org.xmlpull.v1.XmlPullParser;

/** A parser over a compiled XML resource (layout, drawable, xml/, ...). */
public interface XmlResourceParser extends XmlPullParser, AttributeSet, AutoCloseable {
    @Override
    String getAttributeNamespace(int index);

    @Override
    void close();
}
