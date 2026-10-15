package android.widget;

import android.content.Context;
import android.util.AttributeSet;

public class ImageButton extends ImageView {
    public ImageButton(Context context) {
        super(context);
        setFocusable(true);
        setClickable(true);
    }

    public ImageButton(Context context, AttributeSet attrs) {
        super(context, attrs);
        setFocusable(true);
        setClickable(true);
    }
}
