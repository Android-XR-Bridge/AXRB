package android.view;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.Resources;

public class ContextThemeWrapper extends ContextWrapper {
    private int mThemeResource;
    private Resources.Theme mTheme;

    public ContextThemeWrapper() {
        super(null);
    }

    public ContextThemeWrapper(Context base, int themeResId) {
        super(base);
        mThemeResource = themeResId;
    }

    @Override
    protected void attachBaseContext(Context newBase) {
        super.attachBaseContext(newBase);
    }

    @Override
    public void setTheme(int resid) {
        if (mThemeResource != resid) {
            mThemeResource = resid;
            mTheme = null;
        }
    }

    /** @hide */
    public int getThemeResId() {
        return mThemeResource;
    }

    public Resources.Theme getTheme() {
        if (mTheme == null) {
            mTheme = getResources().newTheme();
            mTheme.applyStyle(mThemeResource, true);
        }
        return mTheme;
    }
}
