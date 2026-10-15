package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;

/** Holds an adapter and a selection; item views are not laid out by this runtime. */
public abstract class AdapterView<T extends Adapter> extends ViewGroup {
    public static final int INVALID_POSITION = -1;
    public static final long INVALID_ROW_ID = Long.MIN_VALUE;

    public interface OnItemSelectedListener {
        void onItemSelected(AdapterView<?> parent, View view, int position, long id);

        void onNothingSelected(AdapterView<?> parent);
    }

    public interface OnItemClickListener {
        void onItemClick(AdapterView<?> parent, View view, int position, long id);
    }

    public interface OnItemLongClickListener {
        boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id);
    }

    private OnItemSelectedListener mOnItemSelectedListener;
    private OnItemClickListener mOnItemClickListener;
    int mSelectedPosition = INVALID_POSITION;

    public AdapterView(Context context) {
        super(context);
    }

    public AdapterView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public AdapterView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public abstract T getAdapter();

    public abstract void setAdapter(T adapter);

    public abstract View getSelectedView();

    public void setSelection(int position) {
        T adapter = getAdapter();
        if (adapter == null || position < 0 || position >= adapter.getCount()) return;
        if (mSelectedPosition == position) return;
        mSelectedPosition = position;
        if (mOnItemSelectedListener != null) {
            mOnItemSelectedListener.onItemSelected(this, null, position, adapter.getItemId(position));
        }
    }

    public int getSelectedItemPosition() {
        return mSelectedPosition;
    }

    public Object getSelectedItem() {
        return getItemAtPosition(mSelectedPosition);
    }

    public Object getItemAtPosition(int position) {
        T adapter = getAdapter();
        return adapter == null || position < 0 || position >= adapter.getCount() ? null : adapter.getItem(position);
    }

    public int getCount() {
        T adapter = getAdapter();
        return adapter == null ? 0 : adapter.getCount();
    }

    public void setOnItemSelectedListener(OnItemSelectedListener listener) {
        mOnItemSelectedListener = listener;
    }

    public final OnItemSelectedListener getOnItemSelectedListener() {
        return mOnItemSelectedListener;
    }

    public void setOnItemClickListener(OnItemClickListener listener) {
        mOnItemClickListener = listener;
    }

    public final OnItemClickListener getOnItemClickListener() {
        return mOnItemClickListener;
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {}
}
