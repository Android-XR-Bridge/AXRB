package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.TextPaint;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import java.util.ArrayList;
import java.util.Locale;

/**
 * Displays text with line breaks at '\n' and word wrapping to the available
 * width. Defaults follow Widget.Material.TextView under a light theme
 * (TextAppearance.Material.Small: 14sp, textColorSecondary).
 */
public class TextView extends View {
    /** ?android:textColorSecondary on Theme.Material.Light (secondary_text_default_material_light). */
    static final int DEFAULT_TEXT_COLOR = 0x8A000000;

    private CharSequence mText = "";
    private CharSequence mHint;
    private int mTextColor = DEFAULT_TEXT_COLOR;
    private int mHintColor = 0x61000000;
    private int mGravity = Gravity.TOP | Gravity.START;
    private boolean mAllCaps;
    private int mMaxLines = Integer.MAX_VALUE;
    private boolean mSingleLine;
    private float mSpacingMult = 1f;
    private float mSpacingAdd;
    final TextPaint mTextPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);

    private ArrayList<String> mLines = new ArrayList<String>();
    private int mLayoutWidth = -1;

    public TextView(Context context) {
        super(context);
        initTextSize(context);
    }

    public TextView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public TextView(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    /** View attributes (id, padding, ...) plus text, hint, textSize and textColor. */
    public TextView(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        initTextSize(context);
        if (attrs == null) return;
        android.content.res.TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.TextView,
                defStyleAttr, defStyleRes);
        for (int i = 0, n = a.getIndexCount(); i < n; i++) {
            int attr = a.getIndex(i);
            switch (attr) {
                case android.R.styleable.TextView_text:
                    CharSequence text = a.getText(attr);
                    if (text != null) setText(text);
                    break;
                case android.R.styleable.TextView_hint:
                    CharSequence hint = a.getText(attr);
                    if (hint != null) setHint(hint);
                    break;
                case android.R.styleable.TextView_textSize:
                    int size = a.getDimensionPixelSize(attr, -1);
                    if (size > 0) setTextSize(TypedValue.COMPLEX_UNIT_PX, size);
                    break;
                case android.R.styleable.TextView_textColor:
                    int color = a.getColor(attr, 0);
                    if (color != 0) setTextColor(color);
                    break;
                default:
                    break;
            }
        }
        a.recycle();
    }

    private void initTextSize(Context context) {
        mTextPaint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 14,
                context.getResources().getDisplayMetrics()));
    }

    // --- text -----------------------------------------------------------------

    public CharSequence getText() {
        return mText;
    }

    public int length() {
        return mText.length();
    }

    public final void setText(CharSequence text) {
        setText(text, BufferType.NORMAL);
    }

    public void setText(CharSequence text, BufferType type) {
        if (text == null) text = "";
        CharSequence old = mText;
        if (mWatchers != null) {
            for (android.text.TextWatcher w : new ArrayList<android.text.TextWatcher>(mWatchers)) {
                w.beforeTextChanged(old, 0, old.length(), text.length());
            }
        }
        mText = text;
        textChanged();
        if (mWatchers != null) {
            for (android.text.TextWatcher w : new ArrayList<android.text.TextWatcher>(mWatchers)) {
                w.onTextChanged(text, 0, old.length(), text.length());
            }
            if (text instanceof android.text.Editable) {
                for (android.text.TextWatcher w : new ArrayList<android.text.TextWatcher>(mWatchers)) {
                    w.afterTextChanged((android.text.Editable) text);
                }
            }
        }
    }

    public final void setText(int resid) {
        setText(getContext().getResources().getText(resid));
    }

    public final void setText(char[] text, int start, int len) {
        setText(new String(text, start, len));
    }

    public final void append(CharSequence text) {
        append(text, 0, text.length());
    }

    public void append(CharSequence text, int start, int end) {
        setText(mText.toString() + text.subSequence(start, end));
    }

    public CharSequence getHint() {
        return mHint;
    }

    public final void setHint(CharSequence hint) {
        mHint = hint;
        if (mText.length() == 0) textChanged();
    }

    public enum BufferType {
        NORMAL, SPANNABLE, EDITABLE
    }

    public interface OnEditorActionListener {
        boolean onEditorAction(TextView v, int actionId, android.view.KeyEvent event);
    }

    private OnEditorActionListener mEditorActionListener;
    private final ArrayList<android.text.TextWatcher> mWatchers = new ArrayList<android.text.TextWatcher>();
    private android.text.InputFilter[] mFilters = new android.text.InputFilter[0];
    private int mInputType = android.text.InputType.TYPE_CLASS_TEXT;
    private int mImeOptions;
    private boolean mCursorVisible = true;
    private int mHighlightColor = 0x6633B5E5;

    public void setOnEditorActionListener(OnEditorActionListener l) {
        mEditorActionListener = l;
    }

    public void onEditorAction(int actionCode) {
        if (mEditorActionListener != null) mEditorActionListener.onEditorAction(this, actionCode, null);
    }

    public void addTextChangedListener(android.text.TextWatcher watcher) {
        if (!mWatchers.contains(watcher)) mWatchers.add(watcher);
    }

    public void removeTextChangedListener(android.text.TextWatcher watcher) {
        mWatchers.remove(watcher);
    }

    public void setFilters(android.text.InputFilter[] filters) {
        if (filters == null) throw new IllegalArgumentException();
        mFilters = filters;
    }

    public android.text.InputFilter[] getFilters() {
        return mFilters;
    }

    public void setInputType(int type) {
        mInputType = type;
    }

    public int getInputType() {
        return mInputType;
    }

    public void setRawInputType(int type) {
        mInputType = type;
    }

    public void setImeOptions(int imeOptions) {
        mImeOptions = imeOptions;
    }

    public int getImeOptions() {
        return mImeOptions;
    }

    public void setCursorVisible(boolean visible) {
        mCursorVisible = visible;
    }

    public boolean isCursorVisible() {
        return mCursorVisible;
    }

    public void setHighlightColor(int color) {
        mHighlightColor = color;
    }

    public int getHighlightColor() {
        return mHighlightColor;
    }

    public void setSelectAllOnFocus(boolean selectAllOnFocus) {}

    public void setHorizontallyScrolling(boolean whether) {}

    public void setTextIsSelectable(boolean selectable) {}

    public void setEllipsize(Object where) {}

    private void textChanged() {
        mLayoutWidth = -1;
        requestLayout();
        invalidate();
    }

    // --- appearance -----------------------------------------------------------

    public void setTextColor(int color) {
        mTextColor = color;
        invalidate();
    }

    public final int getCurrentTextColor() {
        return mTextColor;
    }

    public final void setHintTextColor(int color) {
        mHintColor = color;
        invalidate();
    }

    public void setTextSize(float size) {
        setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
    }

    public void setTextSize(int unit, float size) {
        float px = TypedValue.applyDimension(unit, size, getContext().getResources().getDisplayMetrics());
        if (px != mTextPaint.getTextSize()) {
            mTextPaint.setTextSize(px);
            textChanged();
        }
    }

    public float getTextSize() {
        return mTextPaint.getTextSize();
    }

    public void setTypeface(Typeface tf) {
        if (mTextPaint.getTypeface() != tf) {
            mTextPaint.setTypeface(tf);
            textChanged();
        }
    }

    public void setTypeface(Typeface tf, int style) {
        setTypeface(style > 0 ? Typeface.create(tf, style) : tf);
    }

    public Typeface getTypeface() {
        return mTextPaint.getTypeface();
    }

    public TextPaint getPaint() {
        return mTextPaint;
    }

    public void setAllCaps(boolean allCaps) {
        mAllCaps = allCaps;
        textChanged();
    }

    public boolean isAllCaps() {
        return mAllCaps;
    }

    public void setGravity(int gravity) {
        if ((gravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) == 0) gravity |= Gravity.START;
        if ((gravity & Gravity.VERTICAL_GRAVITY_MASK) == 0) gravity |= Gravity.TOP;
        if (gravity != mGravity) {
            mGravity = gravity;
            invalidate();
            requestLayout();
        }
    }

    public int getGravity() {
        return mGravity;
    }

    public void setMaxLines(int maxLines) {
        mMaxLines = maxLines;
        textChanged();
    }

    public int getMaxLines() {
        return mMaxLines;
    }

    public void setLines(int lines) {
        mMaxLines = lines;
        textChanged();
    }

    public void setSingleLine() {
        setSingleLine(true);
    }

    public void setSingleLine(boolean singleLine) {
        mSingleLine = singleLine;
        mMaxLines = singleLine ? 1 : Integer.MAX_VALUE;
        textChanged();
    }

    public void setLineSpacing(float add, float mult) {
        mSpacingAdd = add;
        mSpacingMult = mult;
        textChanged();
    }

    public int getLineCount() {
        return mLines.size();
    }

    public int getLineHeight() {
        return Math.round(mTextPaint.getFontSpacing() * mSpacingMult + mSpacingAdd);
    }

    // --- layout ---------------------------------------------------------------

    private String displayText() {
        CharSequence t = mText.length() == 0 && mHint != null ? mHint : mText;
        String s = t.toString();
        return mAllCaps ? s.toUpperCase(Locale.getDefault()) : s;
    }

    /** Breaks the display text into lines no wider than `width` (<= 0: unlimited). */
    private void makeLines(int width) {
        ArrayList<String> lines = new ArrayList<String>();
        String s = displayText();
        int start = 0;
        for (;;) {
            int nl = mSingleLine ? -1 : s.indexOf('\n', start);
            String para = nl < 0 ? s.substring(start) : s.substring(start, nl);
            if (mSingleLine) para = para.replace('\n', ' ');
            wrap(para, width, lines);
            if (nl < 0) break;
            start = nl + 1;
        }
        while (lines.size() > mMaxLines) lines.remove(lines.size() - 1);
        mLines = lines;
        mLayoutWidth = width;
    }

    private void wrap(String para, int width, ArrayList<String> out) {
        if (width <= 0 || mSingleLine || mTextPaint.measureText(para) <= width) {
            out.add(para);
            return;
        }
        int lineStart = 0;
        while (lineStart < para.length()) {
            int lastBreak = -1;
            int end = lineStart;
            while (end < para.length()) {
                int next = end + 1;
                if (mTextPaint.measureText(para, lineStart, next) > width) break;
                if (para.charAt(end) == ' ') lastBreak = next;
                end = next;
            }
            if (end >= para.length()) {
                out.add(para.substring(lineStart));
                return;
            }
            int cut = lastBreak > lineStart ? lastBreak : Math.max(end, lineStart + 1);
            out.add(trimTrailingSpaces(para.substring(lineStart, cut)));
            lineStart = cut;
            while (lineStart < para.length() && para.charAt(lineStart) == ' ') lineStart++;
        }
    }

    private static String trimTrailingSpaces(String s) {
        int e = s.length();
        while (e > 0 && s.charAt(e - 1) == ' ') e--;
        return s.substring(0, e);
    }

    private int desiredWidth() {
        int widest = 0;
        String s = displayText();
        int start = 0;
        for (;;) {
            int nl = mSingleLine ? -1 : s.indexOf('\n', start);
            String para = nl < 0 ? s.substring(start) : s.substring(start, nl);
            widest = Math.max(widest, (int) Math.ceil(mTextPaint.measureText(para)));
            if (nl < 0) break;
            start = nl + 1;
        }
        return widest;
    }

    private int textHeight(int lineCount) {
        if (lineCount == 0) lineCount = 1;
        Paint.FontMetrics fm = mTextPaint.getFontMetrics();
        int first = (int) Math.ceil(fm.bottom - fm.top);
        int rest = lineCount > 1 ? (lineCount - 1) * getLineHeight() : 0;
        return first + rest;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        int widthSize = MeasureSpec.getSize(widthMeasureSpec);
        int heightSize = MeasureSpec.getSize(heightMeasureSpec);
        int padH = getPaddingLeft() + getPaddingRight();
        int padV = getPaddingTop() + getPaddingBottom();

        int width;
        if (widthMode == MeasureSpec.EXACTLY) {
            width = widthSize;
        } else {
            width = desiredWidth() + padH;
            width = Math.max(width, getSuggestedMinimumWidth());
            if (widthMode == MeasureSpec.AT_MOST) width = Math.min(widthSize, width);
        }
        int want = width - padH;
        makeLines(want);

        int height;
        if (heightMode == MeasureSpec.EXACTLY) {
            height = heightSize;
        } else {
            height = textHeight(mLines.size()) + padV;
            height = Math.max(height, getSuggestedMinimumHeight());
            if (heightMode == MeasureSpec.AT_MOST) height = Math.min(height, heightSize);
        }
        setMeasuredDimension(width, height);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        mLayoutWidth = -1;
    }

    // --- drawing ------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        int avail = getWidth() - getPaddingLeft() - getPaddingRight();
        if (mLayoutWidth != avail) makeLines(avail);
        if (mLines.isEmpty()) return;
        mTextPaint.setColor(mText.length() == 0 ? mHintColor : mTextColor);
        Paint.FontMetrics fm = mTextPaint.getFontMetrics();
        int block = textHeight(mLines.size());
        int innerTop = getPaddingTop();
        int innerHeight = getHeight() - getPaddingTop() - getPaddingBottom();
        int vgrav = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
        float top;
        if (vgrav == Gravity.BOTTOM) {
            top = innerTop + innerHeight - block;
        } else if (vgrav == Gravity.CENTER_VERTICAL) {
            top = innerTop + (innerHeight - block) / 2f;
        } else {
            top = innerTop;
        }
        int hgrav = Gravity.getAbsoluteGravity(mGravity, getLayoutDirection()) & Gravity.HORIZONTAL_GRAVITY_MASK;
        float baseline = top - fm.top;
        int lineHeight = getLineHeight();
        for (int i = 0; i < mLines.size(); i++) {
            String line = mLines.get(i);
            float x = getPaddingLeft();
            if (hgrav != Gravity.LEFT) {
                float w = mTextPaint.measureText(line);
                if (hgrav == Gravity.CENTER_HORIZONTAL) {
                    x += (avail - w) / 2f;
                } else if (hgrav == Gravity.RIGHT) {
                    x += avail - w;
                }
            }
            canvas.drawText(line, x, baseline, mTextPaint);
            baseline += lineHeight;
        }
    }

    @Override
    public String toString() {
        return super.toString() + " text=\"" + mText + "\"";
    }
}
