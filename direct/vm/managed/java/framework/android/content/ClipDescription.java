package android.content;

/** The label and MIME types of a ClipData. */
public class ClipDescription {
    public static final String MIMETYPE_TEXT_PLAIN = "text/plain";
    public static final String MIMETYPE_TEXT_HTML = "text/html";
    public static final String MIMETYPE_TEXT_URILIST = "text/uri-list";
    public static final String MIMETYPE_TEXT_INTENT = "text/vnd.android.intent";
    public static final String MIMETYPE_UNKNOWN = "application/octet-stream";

    private final CharSequence mLabel;
    private final String[] mMimeTypes;
    private long mTimeStamp;

    public ClipDescription(CharSequence label, String[] mimeTypes) {
        if (mimeTypes == null) throw new NullPointerException("mimeTypes is null");
        mLabel = label;
        mMimeTypes = mimeTypes;
    }

    public ClipDescription(ClipDescription o) {
        mLabel = o.mLabel;
        mMimeTypes = o.mMimeTypes;
        mTimeStamp = o.mTimeStamp;
    }

    public static boolean compareMimeTypes(String concreteType, String desiredType) {
        final int typeLength = desiredType.length();
        if (typeLength == 3 && desiredType.equals("*/*")) return true;
        final int slashpos = desiredType.indexOf('/');
        if (slashpos > 0) {
            if (typeLength == slashpos + 2 && desiredType.charAt(slashpos + 1) == '*') {
                if (desiredType.regionMatches(0, concreteType, 0, slashpos + 1)) return true;
            } else if (desiredType.equals(concreteType)) {
                return true;
            }
        }
        return false;
    }

    public CharSequence getLabel() {
        return mLabel;
    }

    public boolean hasMimeType(String mimeType) {
        for (String t : mMimeTypes) {
            if (compareMimeTypes(t, mimeType)) return true;
        }
        return false;
    }

    public String[] filterMimeTypes(String mimeType) {
        java.util.ArrayList<String> array = new java.util.ArrayList<String>();
        for (String t : mMimeTypes) {
            if (compareMimeTypes(t, mimeType)) array.add(t);
        }
        return array.isEmpty() ? null : array.toArray(new String[0]);
    }

    public int getMimeTypeCount() {
        return mMimeTypes.length;
    }

    public String getMimeType(int index) {
        return mMimeTypes[index];
    }

    public long getTimestamp() {
        return mTimeStamp;
    }

    void setTimestamp(long timeStamp) {
        mTimeStamp = timeStamp;
    }

    @Override
    public String toString() {
        StringBuilder b = new StringBuilder("ClipDescription { ");
        for (int i = 0; i < mMimeTypes.length; i++) {
            if (i > 0) b.append(' ');
            b.append(mMimeTypes[i]);
        }
        if (mLabel != null) b.append(" \"").append(mLabel).append('"');
        return b.append(" }").toString();
    }
}
