package android.widget;

import android.content.Context;
import android.util.AttributeSet;

public abstract class CompoundButton extends Button implements Checkable {
    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }

    private boolean mChecked;
    private OnCheckedChangeListener mListener;

    public CompoundButton(Context context) {
        super(context);
    }

    public CompoundButton(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public CompoundButton(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public boolean isChecked() {
        return mChecked;
    }

    public void setChecked(boolean checked) {
        if (mChecked == checked) return;
        mChecked = checked;
        refreshDrawableState();
        if (mListener != null) mListener.onCheckedChanged(this, checked);
    }

    public void toggle() {
        setChecked(!mChecked);
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        mListener = listener;
    }
}
