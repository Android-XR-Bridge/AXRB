package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

public class Spinner extends AdapterView<SpinnerAdapter> {
    private SpinnerAdapter mAdapter;

    public Spinner(Context context) {
        super(context);
    }

    public Spinner(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public Spinner(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public Spinner(Context context, int mode) {
        super(context);
    }

    public SpinnerAdapter getAdapter() {
        return mAdapter;
    }

    public void setAdapter(SpinnerAdapter adapter) {
        mAdapter = adapter;
        mSelectedPosition = adapter != null && adapter.getCount() > 0 ? 0 : INVALID_POSITION;
    }

    public View getSelectedView() {
        return null;
    }
}
