package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.Gravity;

/**
 * Widget.Material.Button under a light theme: btn_default_material (a 2dp
 * rounded colorButtonNormal shape inset 4dp/6dp, 8dp/4dp inner padding),
 * TextAppearance.Material.Button (14sp medium, all caps, textColorPrimary),
 * minWidth 88dp, minHeight 48dp, centered text. The pressed state darkens the
 * shape by the ripple's colorControlHighlight instead of animating a ripple.
 */
public class Button extends TextView {
    static final int BUTTON_NORMAL = 0xFFD6D7D7;
    static final int CONTROL_HIGHLIGHT = 0x1F000000;
    static final int DISABLED_ALPHA = 0x42;
    static final int TEXT_PRIMARY = 0xDE000000;

    private final Paint mShape = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mHighlight = new Paint(Paint.ANTI_ALIAS_FLAG);

    public Button(Context context) {
        super(context);
        initButton();
    }

    private void initButton() {
        float d = density();
        setMinimumWidth(Math.round(88 * d));
        setMinimumHeight(Math.round(48 * d));
        setPadding(Math.round(12 * d), Math.round(10 * d), Math.round(12 * d), Math.round(10 * d));
        setGravity(Gravity.CENTER);
        setAllCaps(true);
        setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        setTextColor(TEXT_PRIMARY);
        setClickable(true);
        setFocusable(true);
        mShape.setColor(BUTTON_NORMAL);
        mHighlight.setColor(CONTROL_HIGHLIGHT);
    }

    public Button(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public Button(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public Button(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        initButton();
    }

    private float density() {
        return getContext().getResources().getDisplayMetrics().density;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float d = density();
        float l = 4 * d, t = 6 * d, r = getWidth() - 4 * d, b = getHeight() - 6 * d;
        float radius = 2 * d;
        int shape = BUTTON_NORMAL;
        if (!isEnabled()) shape = (shape & 0x00FFFFFF) | (DISABLED_ALPHA << 24);
        mShape.setColor(shape);
        canvas.drawRoundRect(l, t, r, b, radius, radius, mShape);
        if (isPressed()) canvas.drawRoundRect(l, t, r, b, radius, radius, mHighlight);
        super.onDraw(canvas);
    }

    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        invalidate();
    }

    public CharSequence getAccessibilityClassName() {
        return Button.class.getName();
    }
}
