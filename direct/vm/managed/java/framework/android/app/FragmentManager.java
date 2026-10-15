package android.app;

import java.util.ArrayList;
import java.util.List;

public class FragmentManager {
    final Activity mActivity;
    final ArrayList<Fragment> mAdded = new ArrayList<Fragment>();

    FragmentManager(Activity activity) {
        mActivity = activity;
    }

    public FragmentTransaction beginTransaction() {
        return new FragmentTransaction(this);
    }

    public Fragment findFragmentByTag(String tag) {
        if (tag == null) return null;
        for (int i = mAdded.size() - 1; i >= 0; i--) {
            Fragment f = mAdded.get(i);
            if (tag.equals(f.mTag)) return f;
        }
        return null;
    }

    public Fragment findFragmentById(int id) {
        for (int i = mAdded.size() - 1; i >= 0; i--) {
            Fragment f = mAdded.get(i);
            if (f.mContainerId == id) return f;
        }
        return null;
    }

    public List<Fragment> getFragments() {
        return new ArrayList<Fragment>(mAdded);
    }

    public boolean executePendingTransactions() {
        return false;
    }

    public boolean isDestroyed() {
        return mActivity.isDestroyed();
    }

    void dispatchResume() {
        for (Fragment f : getFragments()) {
            if (!f.mResumed) {
                f.onStart();
                f.onResume();
                f.mResumed = true;
            }
        }
    }

    void dispatchPause() {
        for (Fragment f : getFragments()) {
            if (f.mResumed) {
                f.onPause();
                f.onStop();
                f.mResumed = false;
            }
        }
    }

    void dispatchDestroy() {
        for (Fragment f : getFragments()) {
            f.performRemove();
        }
        mAdded.clear();
    }

    void dispatchRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        for (Fragment f : getFragments()) f.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }
}
