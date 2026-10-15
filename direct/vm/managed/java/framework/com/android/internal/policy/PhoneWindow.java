package com.android.internal.policy;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

/**
 * The window of an activity: a decor view holding the action bar (for the
 * default DeviceDefault.Light.DarkActionBar theme) above the content frame.
 */
public class PhoneWindow extends Window {
    /** Theme.Material.Light: windowBackground (background_material_light). */
    static final int WINDOW_BACKGROUND = 0xFFFAFAFA;
    /** DarkActionBar: colorPrimary (primary_material_dark). */
    static final int ACTION_BAR_BACKGROUND = 0xFF212121;
    static final int ACTION_BAR_TITLE = 0xFFFFFFFF;

    private DecorView mDecor;
    private ViewGroup mContentParent;
    private TitleBar mTitleBar;
    private CharSequence mTitle;
    private int mBackgroundColor = WINDOW_BACKGROUND;
    private SurfaceView mTakenSurface;

    public PhoneWindow(Context context) {
        super(context);
        requestFeature(FEATURE_ACTION_BAR);
    }

    private void installDecor() {
        if (mDecor != null) return;
        Context c = getContext();
        mDecor = new DecorView(c, this);
        mDecor.setId(View.NO_ID);
        FrameLayout content = new FrameLayout(c);
        content.setId(ID_ANDROID_CONTENT);
        if (hasFeature(FEATURE_ACTION_BAR)) {
            LinearLayout column = new LinearLayout(c);
            column.setOrientation(LinearLayout.VERTICAL);
            mTitleBar = new TitleBar(c);
            mTitleBar.title = mTitle;
            column.addView(mTitleBar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    (int) (56 * density(c) + 0.5f)));
            column.addView(content, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
            mDecor.addView(column, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        } else {
            mDecor.addView(content, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        }
        mContentParent = content;
    }

    static float density(Context c) {
        Resources r = c.getResources();
        return r == null ? 1f : r.getDisplayMetrics().density;
    }

    @Override
    public void setContentView(int layoutResID) {
        installDecor();
        mContentParent.removeAllViews();
        android.view.LayoutInflater.from(getContext()).inflate(layoutResID, mContentParent);
        mContentSet = true;
        Callback cb = getCallback();
        if (cb != null) cb.onContentChanged();
    }

    @Override
    public void setContentView(View view) {
        setContentView(view, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    public void setContentView(View view, ViewGroup.LayoutParams params) {
        installDecor();
        mContentParent.removeAllViews();
        mContentParent.addView(view, params);
        mContentSet = true;
        Callback cb = getCallback();
        if (cb != null) cb.onContentChanged();
    }

    @Override
    public void addContentView(View view, ViewGroup.LayoutParams params) {
        installDecor();
        mContentParent.addView(view, params);
        mContentSet = true;
        Callback cb = getCallback();
        if (cb != null) cb.onContentChanged();
    }

    /**
     * The window surface is a full-window SurfaceView beneath the decor's
     * content, so it outlives setContentView like a real window surface.
     */
    @Override
    public void takeSurface(SurfaceHolder.Callback2 callback) {
        installDecor();
        if (mTakenSurface != null) {
            mDecor.removeView(mTakenSurface);
            mTakenSurface = null;
        }
        if (callback == null) return;
        mTakenSurface = new SurfaceView(getContext());
        mTakenSurface.getHolder().addCallback(callback);
        mDecor.addView(mTakenSurface, 0, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    public View getDecorView() {
        installDecor();
        return mDecor;
    }

    @Override
    public View peekDecorView() {
        return mDecor;
    }

    public int getBackgroundColor() {
        return mBackgroundColor;
    }

    @Override
    public void setBackgroundColor(int color) {
        mBackgroundColor = color;
        if (mDecor != null) mDecor.invalidate();
    }

    @Override
    public void setTitle(CharSequence title) {
        mTitle = title;
        if (mTitleBar != null) {
            mTitleBar.title = title;
            mTitleBar.invalidate();
        }
    }

    public CharSequence getTitle() {
        return mTitle;
    }

    @Override
    public boolean superDispatchTouchEvent(MotionEvent event) {
        return mDecor != null && mDecor.superDispatchTouchEvent(event);
    }

    static final class DecorView extends FrameLayout {
        private final PhoneWindow mWindow;

        DecorView(Context context, PhoneWindow window) {
            super(context);
            mWindow = window;
        }

        @Override
        public boolean dispatchTouchEvent(MotionEvent ev) {
            Callback cb = mWindow.getCallback();
            return cb != null ? cb.dispatchTouchEvent(ev) : super.dispatchTouchEvent(ev);
        }

        boolean superDispatchTouchEvent(MotionEvent ev) {
            return super.dispatchTouchEvent(ev);
        }

        @Override
        protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            Callback cb = mWindow.getCallback();
            if (cb != null) cb.onAttachedToWindow();
        }

        @Override
        protected void onDetachedFromWindow() {
            super.onDetachedFromWindow();
            Callback cb = mWindow.getCallback();
            if (cb != null) cb.onDetachedFromWindow();
        }
    }

    /** The action bar: title on colorPrimary (Widget.Material.ActionBar metrics). */
    static final class TitleBar extends View {
        CharSequence title;
        private final Paint background = new Paint();
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

        TitleBar(Context context) {
            super(context);
            background.setColor(ACTION_BAR_BACKGROUND);
            text.setColor(ACTION_BAR_TITLE);
            text.setTextSize(20 * density(context));
            text.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            canvas.drawRect(0, 0, getWidth(), getHeight(), background);
            if (title == null || title.length() == 0) return;
            Paint.FontMetrics fm = text.getFontMetrics();
            float baseline = (getHeight() - (fm.descent - fm.ascent)) / 2f - fm.ascent;
            canvas.drawText(title.toString(), 16 * density(getContext()), baseline, text);
        }
    }
}
