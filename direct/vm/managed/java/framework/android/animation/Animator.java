package android.animation;

import java.util.ArrayList;

/** The base of property animations. */
public abstract class Animator implements Cloneable {
    public static final long DURATION_INFINITE = -1;

    public interface AnimatorListener {
        default void onAnimationStart(Animator animation, boolean isReverse) {
            onAnimationStart(animation);
        }

        default void onAnimationEnd(Animator animation, boolean isReverse) {
            onAnimationEnd(animation);
        }

        void onAnimationStart(Animator animation);

        void onAnimationEnd(Animator animation);

        void onAnimationCancel(Animator animation);

        void onAnimationRepeat(Animator animation);
    }

    public interface AnimatorPauseListener {
        void onAnimationPause(Animator animation);

        void onAnimationResume(Animator animation);
    }

    ArrayList<AnimatorListener> mListeners = null;
    ArrayList<AnimatorPauseListener> mPauseListeners = null;
    boolean mPaused = false;

    public void start() {}

    public void cancel() {}

    public void end() {}

    public void pause() {
        if (isStarted() && !mPaused) {
            mPaused = true;
            if (mPauseListeners != null) {
                for (AnimatorPauseListener l : new ArrayList<AnimatorPauseListener>(mPauseListeners)) {
                    l.onAnimationPause(this);
                }
            }
        }
    }

    public void resume() {
        if (mPaused) {
            mPaused = false;
            if (mPauseListeners != null) {
                for (AnimatorPauseListener l : new ArrayList<AnimatorPauseListener>(mPauseListeners)) {
                    l.onAnimationResume(this);
                }
            }
        }
    }

    public boolean isPaused() {
        return mPaused;
    }

    public abstract long getStartDelay();

    public abstract void setStartDelay(long startDelay);

    public abstract Animator setDuration(long duration);

    public abstract long getDuration();

    public long getTotalDuration() {
        long duration = getDuration();
        if (duration == DURATION_INFINITE) return DURATION_INFINITE;
        return getStartDelay() + duration;
    }

    public abstract void setInterpolator(TimeInterpolator value);

    public TimeInterpolator getInterpolator() {
        return null;
    }

    public abstract boolean isRunning();

    public boolean isStarted() {
        return isRunning();
    }

    public void addListener(AnimatorListener listener) {
        if (mListeners == null) mListeners = new ArrayList<AnimatorListener>();
        mListeners.add(listener);
    }

    public void removeListener(AnimatorListener listener) {
        if (mListeners == null) return;
        mListeners.remove(listener);
        if (mListeners.size() == 0) mListeners = null;
    }

    public ArrayList<AnimatorListener> getListeners() {
        return mListeners;
    }

    public void addPauseListener(AnimatorPauseListener listener) {
        if (mPauseListeners == null) mPauseListeners = new ArrayList<AnimatorPauseListener>();
        mPauseListeners.add(listener);
    }

    public void removePauseListener(AnimatorPauseListener listener) {
        if (mPauseListeners == null) return;
        mPauseListeners.remove(listener);
        if (mPauseListeners.size() == 0) mPauseListeners = null;
    }

    public void removeAllListeners() {
        mListeners = null;
        mPauseListeners = null;
    }

    @Override
    public Animator clone() {
        try {
            final Animator anim = (Animator) super.clone();
            if (mListeners != null) anim.mListeners = new ArrayList<AnimatorListener>(mListeners);
            if (mPauseListeners != null) anim.mPauseListeners = new ArrayList<AnimatorPauseListener>(mPauseListeners);
            return anim;
        } catch (CloneNotSupportedException e) {
            throw new AssertionError();
        }
    }

    public void setupStartValues() {}

    public void setupEndValues() {}

    public void setTarget(Object target) {}

    ArrayList<AnimatorListener> listenersCopy() {
        return mListeners == null ? null : new ArrayList<AnimatorListener>(mListeners);
    }
}
