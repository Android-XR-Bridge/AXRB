package android.app;

import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;

public class FragmentTransaction {
    private final FragmentManager mManager;
    private final ArrayList<Runnable> mOps = new ArrayList<Runnable>();
    private boolean mCommitted;

    FragmentTransaction(FragmentManager manager) {
        mManager = manager;
    }

    public FragmentTransaction add(Fragment fragment, String tag) {
        return add(0, fragment, tag);
    }

    public FragmentTransaction add(int containerViewId, Fragment fragment) {
        return add(containerViewId, fragment, null);
    }

    public FragmentTransaction add(final int containerViewId, final Fragment fragment, final String tag) {
        mOps.add(new Runnable() {
            public void run() {
                if (mManager.mAdded.contains(fragment)) return;
                fragment.mTag = tag;
                fragment.mContainerId = containerViewId;
                fragment.mFragmentManager = mManager;
                mManager.mAdded.add(fragment);
                fragment.performAttachAndCreate(mManager.mActivity);
            }
        });
        return this;
    }

    public FragmentTransaction replace(int containerViewId, Fragment fragment, String tag) {
        Fragment old = mManager.findFragmentById(containerViewId);
        if (old != null) remove(old);
        return add(containerViewId, fragment, tag);
    }

    public FragmentTransaction remove(final Fragment fragment) {
        mOps.add(new Runnable() {
            public void run() {
                if (mManager.mAdded.remove(fragment)) fragment.performRemove();
            }
        });
        return this;
    }

    public FragmentTransaction detach(Fragment fragment) {
        return this;
    }

    public FragmentTransaction attach(Fragment fragment) {
        return this;
    }

    public FragmentTransaction addToBackStack(String name) {
        return this;
    }

    public FragmentTransaction setTransition(int transit) {
        return this;
    }

    private void run() {
        for (Runnable op : mOps) op.run();
    }

    public int commit() {
        if (mCommitted) throw new IllegalStateException("commit already called");
        mCommitted = true;
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            public void run() {
                FragmentTransaction.this.run();
            }
        });
        return 0;
    }

    public int commitAllowingStateLoss() {
        return commit();
    }

    public void commitNow() {
        mCommitted = true;
        run();
    }

    public void commitNowAllowingStateLoss() {
        commitNow();
    }
}
