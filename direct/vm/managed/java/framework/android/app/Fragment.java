package android.app;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

/** Headless (non-UI) fragments: what applications attach for permission requests and retained state. */
public class Fragment {
    Activity mActivity;
    FragmentManager mFragmentManager;
    String mTag;
    int mContainerId;
    boolean mAdded;
    boolean mRemoving;
    boolean mResumed;
    boolean mCalled;
    private Bundle mArguments;
    private boolean mRetainInstance;

    public Fragment() {}

    public static Fragment instantiate(Context context, String fname, Bundle args) {
        try {
            Fragment f = (Fragment) context.getClassLoader().loadClass(fname).newInstance();
            if (args != null) f.setArguments(args);
            return f;
        } catch (Exception e) {
            throw new RuntimeException("Unable to instantiate fragment " + fname, e);
        }
    }

    public final Activity getActivity() {
        return mActivity;
    }

    public Context getContext() {
        return mActivity;
    }

    public final FragmentManager getFragmentManager() {
        return mFragmentManager;
    }

    public final String getTag() {
        return mTag;
    }

    public final int getId() {
        return mContainerId;
    }

    public final boolean isAdded() {
        return mActivity != null && mAdded;
    }

    public final boolean isRemoving() {
        return mRemoving;
    }

    public final boolean isResumed() {
        return mResumed;
    }

    public final boolean isDetached() {
        return false;
    }

    public final boolean isVisible() {
        return isAdded();
    }

    public void setArguments(Bundle args) {
        mArguments = args;
    }

    public final Bundle getArguments() {
        return mArguments;
    }

    public void setRetainInstance(boolean retain) {
        mRetainInstance = retain;
    }

    public final boolean getRetainInstance() {
        return mRetainInstance;
    }

    public final android.content.res.Resources getResources() {
        return mActivity.getResources();
    }

    public final String getString(int resId) {
        return mActivity.getString(resId);
    }

    public void onAttach(Context context) {
        mCalled = true;
    }

    public void onAttach(Activity activity) {
        mCalled = true;
    }

    public void onCreate(Bundle savedInstanceState) {
        mCalled = true;
    }

    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return null;
    }

    public void onActivityCreated(Bundle savedInstanceState) {
        mCalled = true;
    }

    public void onStart() {
        mCalled = true;
    }

    public void onResume() {
        mCalled = true;
    }

    public void onPause() {
        mCalled = true;
    }

    public void onStop() {
        mCalled = true;
    }

    public void onDestroy() {
        mCalled = true;
    }

    public void onDetach() {
        mCalled = true;
    }

    public void onActivityResult(int requestCode, int resultCode, Intent data) {}

    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {}

    public final void requestPermissions(final String[] permissions, final int requestCode) {
        final int[] results = new int[permissions.length];
        for (int i = 0; i < permissions.length; i++) {
            results[i] = mActivity.checkSelfPermission(permissions[i]);
        }
        new android.os.Handler(android.os.Looper.getMainLooper()).post(new Runnable() {
            public void run() {
                onRequestPermissionsResult(requestCode, permissions, results);
            }
        });
    }

    public boolean shouldShowRequestPermissionRationale(String permission) {
        return false;
    }

    public void startActivity(Intent intent) {
        mActivity.startActivity(intent);
    }

    public void startActivityForResult(Intent intent, int requestCode) {
        mActivity.startActivityForResult(intent, requestCode);
    }

    void performAttachAndCreate(Activity activity) {
        mActivity = activity;
        mAdded = true;
        onAttach((Context) activity);
        onAttach(activity);
        onCreate(null);
        onActivityCreated(null);
        if (activity.mResumed) {
            onStart();
            onResume();
            mResumed = true;
        }
    }

    void performRemove() {
        mRemoving = true;
        if (mResumed) {
            onPause();
            onStop();
            mResumed = false;
        }
        onDestroy();
        onDetach();
        mAdded = false;
        mActivity = null;
    }

    public String toString() {
        return getClass().getSimpleName() + "{" + Integer.toHexString(System.identityHashCode(this))
                + (mTag != null ? " " + mTag : "") + "}";
    }
}
