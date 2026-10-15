package android.widget;

import android.content.Context;
import android.text.Editable;
import android.text.InputFilter;
import android.text.SpannableStringBuilder;
import android.util.AttributeSet;

public class EditText extends TextView {
    private int mSelectionStart;
    private int mSelectionEnd;

    public EditText(Context context) {
        this(context, null);
    }

    public EditText(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public EditText(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public EditText(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setText("", BufferType.EDITABLE);
    }

    @Override
    public Editable getText() {
        CharSequence t = super.getText();
        if (t instanceof Editable) return (Editable) t;
        Editable e = new SpannableStringBuilder(t);
        super.setText(e, BufferType.EDITABLE);
        return e;
    }

    @Override
    public void setText(CharSequence text, BufferType type) {
        SpannableStringBuilder e = new SpannableStringBuilder(text != null ? text : "");
        e.setFilters(getFilters());
        super.setText(e, BufferType.EDITABLE);
        mSelectionStart = mSelectionEnd = e.length();
    }

    @Override
    public void setFilters(InputFilter[] filters) {
        super.setFilters(filters);
        CharSequence t = super.getText();
        if (t instanceof Editable) ((Editable) t).setFilters(filters);
    }

    public int getSelectionStart() {
        return Math.min(mSelectionStart, length());
    }

    public int getSelectionEnd() {
        return Math.min(mSelectionEnd, length());
    }

    public void setSelection(int start, int stop) {
        if (start < 0 || stop < 0 || start > length() || stop > length()) {
            throw new IndexOutOfBoundsException("setSelection " + start + ", " + stop + " out of length " + length());
        }
        mSelectionStart = start;
        mSelectionEnd = stop;
    }

    public void setSelection(int index) {
        setSelection(index, index);
    }

    public void selectAll() {
        setSelection(0, length());
    }

    public void extendSelection(int index) {
        mSelectionEnd = index;
    }
}
