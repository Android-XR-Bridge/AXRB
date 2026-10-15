package android.content;

import android.net.Uri;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;

/** Clipped data: a description and one or more items of text, HTML, an intent or a URI. */
public class ClipData {
    static final String[] MIMETYPES_TEXT_PLAIN = {ClipDescription.MIMETYPE_TEXT_PLAIN};
    static final String[] MIMETYPES_TEXT_HTML = {ClipDescription.MIMETYPE_TEXT_HTML};
    static final String[] MIMETYPES_TEXT_URILIST = {ClipDescription.MIMETYPE_TEXT_URILIST};
    static final String[] MIMETYPES_TEXT_INTENT = {ClipDescription.MIMETYPE_TEXT_INTENT};

    public static class Item {
        final CharSequence mText;
        final String mHtmlText;
        final Intent mIntent;
        final Uri mUri;

        public Item(Item other) {
            mText = other.mText;
            mHtmlText = other.mHtmlText;
            mIntent = other.mIntent;
            mUri = other.mUri;
        }

        public Item(CharSequence text) {
            this(text, null, null, null);
        }

        public Item(CharSequence text, String htmlText) {
            this(text, htmlText, null, null);
        }

        public Item(Intent intent) {
            this(null, null, intent, null);
        }

        public Item(Uri uri) {
            this(null, null, null, uri);
        }

        public Item(CharSequence text, Intent intent, Uri uri) {
            this(text, null, intent, uri);
        }

        public Item(CharSequence text, String htmlText, Intent intent, Uri uri) {
            if (htmlText != null && text == null) {
                throw new IllegalArgumentException("Plain text must be supplied if HTML text is supplied");
            }
            mText = text;
            mHtmlText = htmlText;
            mIntent = intent;
            mUri = uri;
        }

        public CharSequence getText() {
            return mText;
        }

        public String getHtmlText() {
            return mHtmlText;
        }

        public Intent getIntent() {
            return mIntent;
        }

        public Uri getUri() {
            return mUri;
        }

        public CharSequence coerceToText(Context context) {
            if (mText != null) return mText;
            if (mUri != null) {
                InputStream in = null;
                try {
                    in = context.getContentResolver().openInputStream(mUri);
                    InputStreamReader reader = new InputStreamReader(in, "UTF-8");
                    StringBuilder builder = new StringBuilder(128);
                    char[] buffer = new char[8192];
                    int len;
                    while ((len = reader.read(buffer)) > 0) builder.append(buffer, 0, len);
                    return builder.toString();
                } catch (IOException | SecurityException e) {
                    return e.toString();
                } finally {
                    if (in != null) {
                        try {
                            in.close();
                        } catch (IOException e) {
                            // Ignored.
                        }
                    }
                }
            }
            if (mIntent != null) return mIntent.toUri(Intent.URI_INTENT_SCHEME);
            return "";
        }

        public CharSequence coerceToStyledText(Context context) {
            return coerceToText(context);
        }

        public String coerceToHtmlText(Context context) {
            if (mHtmlText != null) return mHtmlText;
            CharSequence text = coerceToText(context);
            return text != null ? android.text.TextUtils.htmlEncode(text.toString()) : null;
        }

        @Override
        public String toString() {
            return "ClipData.Item { " + (mHtmlText != null ? "H:" + mHtmlText : mText != null ? "T:" + mText
                    : mUri != null ? "U:" + mUri : mIntent != null ? "I:" + mIntent : "NULL") + " }";
        }
    }

    final ClipDescription mClipDescription;
    final ArrayList<Item> mItems;

    public ClipData(CharSequence label, String[] mimeTypes, Item item) {
        mClipDescription = new ClipDescription(label, mimeTypes);
        if (item == null) throw new NullPointerException("item is null");
        mItems = new ArrayList<Item>();
        mItems.add(item);
    }

    public ClipData(ClipDescription description, Item item) {
        mClipDescription = description;
        if (item == null) throw new NullPointerException("item is null");
        mItems = new ArrayList<Item>();
        mItems.add(item);
    }

    public ClipData(ClipData other) {
        mClipDescription = other.mClipDescription;
        mItems = new ArrayList<Item>(other.mItems);
    }

    public static ClipData newPlainText(CharSequence label, CharSequence text) {
        return new ClipData(label, MIMETYPES_TEXT_PLAIN, new Item(text));
    }

    public static ClipData newHtmlText(CharSequence label, CharSequence text, String htmlText) {
        return new ClipData(label, MIMETYPES_TEXT_HTML, new Item(text, htmlText));
    }

    public static ClipData newIntent(CharSequence label, Intent intent) {
        return new ClipData(label, MIMETYPES_TEXT_INTENT, new Item(intent));
    }

    public static ClipData newUri(ContentResolver resolver, CharSequence label, Uri uri) {
        return new ClipData(label, MIMETYPES_TEXT_URILIST, new Item(uri));
    }

    public static ClipData newRawUri(CharSequence label, Uri uri) {
        return new ClipData(label, MIMETYPES_TEXT_URILIST, new Item(uri));
    }

    public ClipDescription getDescription() {
        return mClipDescription;
    }

    public void addItem(Item item) {
        if (item == null) throw new NullPointerException("item is null");
        mItems.add(item);
    }

    public void addItem(ContentResolver resolver, Item item) {
        addItem(item);
    }

    public int getItemCount() {
        return mItems.size();
    }

    public Item getItemAt(int index) {
        return mItems.get(index);
    }

    @Override
    public String toString() {
        return "ClipData { " + mClipDescription + " " + mItems + " }";
    }
}
