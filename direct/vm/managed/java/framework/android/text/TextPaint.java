package android.text;

import android.graphics.Paint;

public class TextPaint extends Paint {
    public int bgColor;
    public int linkColor;
    public float density = 1.0f;

    public TextPaint() {
        super();
    }

    public TextPaint(int flags) {
        super(flags);
    }

    public TextPaint(Paint p) {
        super(p);
    }
}
