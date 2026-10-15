package android.animation;

import android.view.Choreographer;
import java.util.ArrayList;

/** Drives running animators from the thread's Choreographer frames. */
public class AnimationHandler {
    interface AnimationFrameCallback {
        boolean doAnimationFrame(long frameTime);
    }

    private static final ThreadLocal<AnimationHandler> sAnimatorHandler = new ThreadLocal<AnimationHandler>();

    private final ArrayList<AnimationFrameCallback> mAnimationCallbacks = new ArrayList<AnimationFrameCallback>();
    private boolean mListDirty;
    private boolean mFrameCallbackPosted;

    private final Choreographer.FrameCallback mFrameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            mFrameCallbackPosted = false;
            doAnimationFrame(frameTimeNanos / 1000000L);
            if (!mAnimationCallbacks.isEmpty()) {
                mFrameCallbackPosted = true;
                Choreographer.getInstance().postFrameCallback(this);
            }
        }
    };

    public static AnimationHandler getInstance() {
        AnimationHandler handler = sAnimatorHandler.get();
        if (handler == null) {
            handler = new AnimationHandler();
            sAnimatorHandler.set(handler);
        }
        return handler;
    }

    void addAnimationFrameCallback(AnimationFrameCallback callback) {
        if (!mFrameCallbackPosted) {
            mFrameCallbackPosted = true;
            Choreographer.getInstance().postFrameCallback(mFrameCallback);
        }
        if (!mAnimationCallbacks.contains(callback)) mAnimationCallbacks.add(callback);
    }

    void removeCallback(AnimationFrameCallback callback) {
        int id = mAnimationCallbacks.indexOf(callback);
        if (id >= 0) {
            mAnimationCallbacks.set(id, null);
            mListDirty = true;
        }
    }

    private void doAnimationFrame(long frameTime) {
        final int size = mAnimationCallbacks.size();
        for (int i = 0; i < size; i++) {
            final AnimationFrameCallback callback = mAnimationCallbacks.get(i);
            if (callback == null) continue;
            callback.doAnimationFrame(frameTime);
        }
        if (mListDirty) {
            for (int i = mAnimationCallbacks.size() - 1; i >= 0; i--) {
                if (mAnimationCallbacks.get(i) == null) mAnimationCallbacks.remove(i);
            }
            mListDirty = false;
        }
    }
}
