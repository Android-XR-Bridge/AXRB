package android.content.res;

import android.util.TypedValue;
import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * A compiled (binary) XML document. The host decodes it once into flat event
 * and attribute tables plus its string pool; parsers walk those tables.
 *
 * Runtime-internal (hidden on Android).
 */
public final class XmlBlock {
    static final int EVENT_INTS = 6;  // kind, line, ns, name, firstAttr, attrCount
    static final int ATTR_INTS = 6;   // ns, name, resId, raw, type, data
    private static final int NO_STRING = -1;
    private static final String ANDROID_NS = "http://schemas.android.com/apk/res/android";

    private final int[] mEvents;
    private final int[] mAttrs;
    private final String[] mStrings;
    final String mPath;

    XmlBlock(Object[] parts, String path) {
        mEvents = (int[]) parts[0];
        mAttrs = (int[]) parts[1];
        mStrings = (String[]) parts[2];
        mPath = path;
    }

    public Parser newParser() {
        return new Parser();
    }

    String string(int index) {
        return index >= 0 && index < mStrings.length ? mStrings[index] : null;
    }

    public final class Parser implements XmlResourceParser {
        private int mEvent = -1;  // index into mEvents; -1 = START_DOCUMENT
        private int mEventType = START_DOCUMENT;
        private int mDepth;
        private boolean mDecNextDepth;
        private boolean mClosed;

        public XmlBlock getBlock() {
            return XmlBlock.this;
        }

        /** String `index` of this document's pool (TypedArray values with cookie -1). */
        public CharSequence getPooledString(int index) {
            return string(index);
        }

        // --- raw access for attribute resolution -----------------------------

        private int attrBase() {
            if (mEventType != START_TAG) return -1;
            return mEvents[mEvent * EVENT_INTS + 4];
        }

        /** Number of attributes on the current start tag (0 elsewhere). */
        @Override
        public int getAttributeCount() {
            return mEventType == START_TAG ? mEvents[mEvent * EVENT_INTS + 5] : -1;
        }

        private int attr(int index, int field) {
            int count = getAttributeCount();
            if (index < 0 || index >= count) throw new IndexOutOfBoundsException(String.valueOf(index));
            return mAttrs[(attrBase() + index) * ATTR_INTS + field];
        }

        public int getAttributeDataType(int index) {
            return attr(index, 4);
        }

        public int getAttributeData(int index) {
            return attr(index, 5);
        }

        /** Resource ids, types and data of the current element's attributes. */
        void fillAttributeArrays(int[] ids, int[] types, int[] data) {
            int n = getAttributeCount();
            for (int i = 0; i < n; i++) {
                ids[i] = attr(i, 2);
                types[i] = attr(i, 4);
                data[i] = attr(i, 5);
            }
        }

        private int findPlain(String name) {
            int n = getAttributeCount();
            for (int i = 0; i < n; i++) {
                if (attr(i, 0) == NO_STRING && name.equals(string(attr(i, 1)))) return i;
            }
            return -1;
        }

        // --- XmlPullParser ---------------------------------------------------------

        @Override
        public void setFeature(String name, boolean state) throws XmlPullParserException {
            if (FEATURE_PROCESS_NAMESPACES.equals(name) && state) return;
            if (FEATURE_REPORT_NAMESPACE_ATTRIBUTES.equals(name) && state) return;
            throw new XmlPullParserException("Unsupported feature: " + name);
        }

        @Override
        public boolean getFeature(String name) {
            return FEATURE_PROCESS_NAMESPACES.equals(name) || FEATURE_REPORT_NAMESPACE_ATTRIBUTES.equals(name);
        }

        @Override
        public void setProperty(String name, Object value) throws XmlPullParserException {
            throw new XmlPullParserException("setProperty() not supported");
        }

        @Override
        public Object getProperty(String name) {
            return null;
        }

        @Override
        public void setInput(Reader in) throws XmlPullParserException {
            throw new XmlPullParserException("setInput() not supported");
        }

        @Override
        public void setInput(InputStream inputStream, String inputEncoding) throws XmlPullParserException {
            throw new XmlPullParserException("setInput() not supported");
        }

        @Override
        public void defineEntityReplacementText(String entityName, String replacementText)
                throws XmlPullParserException {
            throw new XmlPullParserException("defineEntityReplacementText() not supported");
        }

        @Override
        public String getNamespacePrefix(int pos) throws XmlPullParserException {
            throw new XmlPullParserException("getNamespacePrefix() not supported");
        }

        @Override
        public String getInputEncoding() {
            return null;
        }

        @Override
        public String getNamespace(String prefix) {
            throw new RuntimeException("getNamespace() not supported");
        }

        @Override
        public int getNamespaceCount(int depth) throws XmlPullParserException {
            throw new XmlPullParserException("getNamespaceCount() not supported");
        }

        @Override
        public String getPositionDescription() {
            return "Binary XML file line #" + getLineNumber();
        }

        @Override
        public String getNamespaceUri(int pos) throws XmlPullParserException {
            throw new XmlPullParserException("getNamespaceUri() not supported");
        }

        @Override
        public int getColumnNumber() {
            return -1;
        }

        @Override
        public int getDepth() {
            return mDepth;
        }

        @Override
        public String getText() {
            if (mEventType != TEXT) return null;
            return string(mEvents[mEvent * EVENT_INTS + 3]);
        }

        @Override
        public int getLineNumber() {
            return mEvent >= 0 && mEvent * EVENT_INTS < mEvents.length ? mEvents[mEvent * EVENT_INTS + 1] : -1;
        }

        @Override
        public int getEventType() throws XmlPullParserException {
            return mEventType;
        }

        @Override
        public boolean isWhitespace() throws XmlPullParserException {
            String t = getText();
            if (t == null) return false;
            for (int i = 0; i < t.length(); i++) {
                char c = t.charAt(i);
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
            }
            return true;
        }

        @Override
        public String getPrefix() {
            throw new RuntimeException("getPrefix not supported");
        }

        @Override
        public char[] getTextCharacters(int[] holderForStartAndLength) {
            String txt = getText();
            char[] chars = null;
            if (txt != null) {
                holderForStartAndLength[0] = 0;
                holderForStartAndLength[1] = txt.length();
                chars = new char[txt.length()];
                txt.getChars(0, txt.length(), chars, 0);
            }
            return chars;
        }

        @Override
        public String getNamespace() {
            if (mEventType != START_TAG && mEventType != END_TAG) return "";
            String ns = string(mEvents[mEvent * EVENT_INTS + 2]);
            return ns != null ? ns : "";
        }

        @Override
        public String getName() {
            if (mEventType != START_TAG && mEventType != END_TAG) return null;
            return string(mEvents[mEvent * EVENT_INTS + 3]);
        }

        @Override
        public String getAttributeNamespace(int index) {
            String ns = string(attr(index, 0));
            return ns != null ? ns : "";
        }

        @Override
        public String getAttributeName(int index) {
            String name = string(attr(index, 1));
            if (name == null) throw new IndexOutOfBoundsException(String.valueOf(index));
            return name;
        }

        @Override
        public String getAttributePrefix(int index) {
            throw new RuntimeException("getAttributePrefix not supported");
        }

        @Override
        public boolean isEmptyElementTag() throws XmlPullParserException {
            return false;
        }

        @Override
        public String getAttributeValue(int index) {
            int raw = attr(index, 3);
            if (raw != NO_STRING) return string(raw);
            int type = attr(index, 4);
            if (type == TypedValue.TYPE_STRING) return string(attr(index, 5));
            if (type == TypedValue.TYPE_NULL) throw new IndexOutOfBoundsException(String.valueOf(index));
            return TypedValue.coerceToString(type, attr(index, 5));
        }

        @Override
        public String getAttributeType(int index) {
            return "CDATA";
        }

        @Override
        public boolean isAttributeDefault(int index) {
            return false;
        }

        private int find(String namespace, String name) {
            int n = getAttributeCount();
            for (int i = 0; i < n; i++) {
                if (!name.equals(string(attr(i, 1)))) continue;
                if (namespace == null) return i;
                String ns = string(attr(i, 0));
                if (namespace.equals(ns != null ? ns : "")) return i;
            }
            return -1;
        }

        @Override
        public String getAttributeValue(String namespace, String name) {
            int idx = find(namespace, name);
            return idx >= 0 ? getAttributeValue(idx) : null;
        }

        @Override
        public int next() throws XmlPullParserException, IOException {
            if (mClosed) throw new XmlPullParserException("Parser has been closed");
            if (mDecNextDepth) {
                mDepth--;
                mDecNextDepth = false;
            }
            if (mEventType == END_DOCUMENT) return END_DOCUMENT;
            mEvent++;
            if (mEvent * EVENT_INTS >= mEvents.length) {
                mEventType = END_DOCUMENT;
                return END_DOCUMENT;
            }
            mEventType = mEvents[mEvent * EVENT_INTS];
            if (mEventType == START_TAG) {
                mDepth++;
            } else if (mEventType == END_TAG) {
                mDecNextDepth = true;
            }
            return mEventType;
        }

        @Override
        public int nextToken() throws XmlPullParserException, IOException {
            return next();
        }

        @Override
        public void require(int type, String namespace, String name) throws XmlPullParserException, IOException {
            if (type != getEventType() || (namespace != null && !namespace.equals(getNamespace()))
                    || (name != null && !name.equals(getName()))) {
                throw new XmlPullParserException("expected " + TYPES[type] + getPositionDescription());
            }
        }

        @Override
        public String nextText() throws XmlPullParserException, IOException {
            if (getEventType() != START_TAG) {
                throw new XmlPullParserException(getPositionDescription()
                        + ": parser must be on START_TAG to read next text", this, null);
            }
            int eventType = next();
            if (eventType == TEXT) {
                String result = getText();
                eventType = next();
                if (eventType != END_TAG) {
                    throw new XmlPullParserException(getPositionDescription()
                            + ": event TEXT it must be immediately followed by END_TAG", this, null);
                }
                return result;
            } else if (eventType == END_TAG) {
                return "";
            }
            throw new XmlPullParserException(getPositionDescription()
                    + ": parser must be on START_TAG or TEXT to read text", this, null);
        }

        @Override
        public int nextTag() throws XmlPullParserException, IOException {
            int eventType = next();
            if (eventType == TEXT && isWhitespace()) eventType = next();
            if (eventType != START_TAG && eventType != END_TAG) {
                throw new XmlPullParserException(getPositionDescription() + ": expected start or end tag", this,
                        null);
            }
            return eventType;
        }

        // --- AttributeSet -----------------------------------------------------------

        @Override
        public int getAttributeNameResource(int index) {
            return attr(index, 2);
        }

        @Override
        public int getAttributeListValue(String namespace, String attribute, String[] options, int defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeListValue(idx, options, defaultValue) : defaultValue;
        }

        @Override
        public boolean getAttributeBooleanValue(String namespace, String attribute, boolean defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeBooleanValue(idx, defaultValue) : defaultValue;
        }

        @Override
        public int getAttributeResourceValue(String namespace, String attribute, int defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeResourceValue(idx, defaultValue) : defaultValue;
        }

        @Override
        public int getAttributeIntValue(String namespace, String attribute, int defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeIntValue(idx, defaultValue) : defaultValue;
        }

        @Override
        public int getAttributeUnsignedIntValue(String namespace, String attribute, int defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeUnsignedIntValue(idx, defaultValue) : defaultValue;
        }

        @Override
        public float getAttributeFloatValue(String namespace, String attribute, float defaultValue) {
            int idx = find(namespace, attribute);
            return idx >= 0 ? getAttributeFloatValue(idx, defaultValue) : defaultValue;
        }

        @Override
        public int getAttributeListValue(int idx, String[] options, int defaultValue) {
            int t = getAttributeDataType(idx);
            int v = getAttributeData(idx);
            if (t == TypedValue.TYPE_STRING) {
                String s = string(v);
                for (int i = 0; options != null && i < options.length; i++) {
                    if (options[i].equals(s)) return i;
                }
                return defaultValue;
            }
            return v;
        }

        @Override
        public boolean getAttributeBooleanValue(int idx, boolean defaultValue) {
            int t = getAttributeDataType(idx);
            if (t >= TypedValue.TYPE_FIRST_INT && t <= TypedValue.TYPE_LAST_INT) return getAttributeData(idx) != 0;
            return defaultValue;
        }

        @Override
        public int getAttributeResourceValue(int idx, int defaultValue) {
            int t = getAttributeDataType(idx);
            if (t == TypedValue.TYPE_REFERENCE) return getAttributeData(idx);
            return defaultValue;
        }

        @Override
        public int getAttributeIntValue(int idx, int defaultValue) {
            int t = getAttributeDataType(idx);
            if (t >= TypedValue.TYPE_FIRST_INT && t <= TypedValue.TYPE_LAST_INT) return getAttributeData(idx);
            return defaultValue;
        }

        @Override
        public int getAttributeUnsignedIntValue(int idx, int defaultValue) {
            return getAttributeIntValue(idx, defaultValue);
        }

        @Override
        public float getAttributeFloatValue(int idx, float defaultValue) {
            int t = getAttributeDataType(idx);
            if (t == TypedValue.TYPE_FLOAT) return Float.intBitsToFloat(getAttributeData(idx));
            throw new RuntimeException("not a float!");
        }

        @Override
        public String getIdAttribute() {
            int idx = find(ANDROID_NS, "id");
            return idx >= 0 ? getAttributeValue(idx) : null;
        }

        @Override
        public String getClassAttribute() {
            int idx = findPlain("class");
            return idx >= 0 ? getAttributeValue(idx) : null;
        }

        @Override
        public int getIdAttributeResourceValue(int defaultValue) {
            return getAttributeResourceValue(ANDROID_NS, "id", defaultValue);
        }

        @Override
        public int getStyleAttribute() {
            int idx = findPlain("style");
            if (idx < 0) return 0;
            int t = getAttributeDataType(idx);
            return t == TypedValue.TYPE_REFERENCE || t == TypedValue.TYPE_ATTRIBUTE ? getAttributeData(idx) : 0;
        }

        /** The plain "style" attribute as {type, data}, or {TYPE_NULL, 0}. */
        int[] styleAttributeValue() {
            int idx = findPlain("style");
            if (idx < 0) return new int[] {TypedValue.TYPE_NULL, 0};
            return new int[] {getAttributeDataType(idx), getAttributeData(idx)};
        }

        @Override
        public void close() {
            mClosed = true;
        }

        @Override
        public String toString() {
            return "XmlBlock.Parser{" + mPath + " " + getPositionDescription() + "}";
        }
    }
}
