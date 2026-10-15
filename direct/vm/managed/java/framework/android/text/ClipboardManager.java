package android.text;

/** The legacy text-only clipboard interface. */
@Deprecated
public abstract class ClipboardManager {
    public abstract CharSequence getText();

    public abstract void setText(CharSequence text);

    public abstract boolean hasText();
}
