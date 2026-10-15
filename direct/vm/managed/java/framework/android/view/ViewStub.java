package android.view;

import android.content.Context;
import android.util.AttributeSet;

/** An invisible, zero-sized placeholder. Inflation of the deferred layout is not supported. */
public final class ViewStub extends View {
    private int mLayoutResource;
    private int mInflatedId;

    public ViewStub(Context context) {
        super(context);
        setVisibility(GONE);
    }

    public ViewStub(Context context, AttributeSet attrs) {
        super(context, attrs);
        setVisibility(GONE);
    }

    private LayoutInflater mInflater;

    public void setLayoutInflater(LayoutInflater inflater) {
        mInflater = inflater;
    }

    public LayoutInflater getLayoutInflater() {
        return mInflater;
    }

    public int getLayoutResource() {
        return mLayoutResource;
    }

    public void setLayoutResource(int layoutResource) {
        mLayoutResource = layoutResource;
    }

    public int getInflatedId() {
        return mInflatedId;
    }

    public void setInflatedId(int inflatedId) {
        mInflatedId = inflatedId;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(0, 0);
    }
}
