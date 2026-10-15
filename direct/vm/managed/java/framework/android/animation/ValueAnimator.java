package android.animation;

import android.os.Looper;
import android.view.animation.AccelerateDecelerateInterpolator;
import android.view.animation.AnimationUtils;
import android.view.animation.LinearInterpolator;
import java.util.ArrayList;
import java.util.HashMap;

/** Animates values over time on the main thread's frames (AOSP ValueAnimator semantics). */
public class ValueAnimator extends Animator implements AnimationHandler.AnimationFrameCallback {
    public static final int RESTART = 1;
    public static final int REVERSE = 2;
    public static final int INFINITE = -1;

    private static float sDurationScale = 1.0f;
    private static final TimeInterpolator sDefaultInterpolator = new AccelerateDecelerateInterpolator();

    public interface AnimatorUpdateListener {
        void onAnimationUpdate(ValueAnimator animation);
    }

    long mStartTime = -1;
    private long mPauseTime;
    private boolean mResumed = false;
    private boolean mReversing;
    private float mOverallFraction = 0f;
    private float mCurrentFraction = 0f;
    private long mLastFrameTime = -1;
    private boolean mRunning = false;
    private boolean mStarted = false;
    private boolean mStartListenersCalled = false;
    boolean mInitialized = false;
    private boolean mAnimationEndRequested = false;
    private long mDuration = 300;
    private long mStartDelay = 0;
    private int mRepeatCount = 0;
    private int mRepeatMode = RESTART;
    private float mSeekFraction = -1;
    private TimeInterpolator mInterpolator = sDefaultInterpolator;
    ArrayList<AnimatorUpdateListener> mUpdateListeners = null;
    PropertyValuesHolder[] mValues;
    HashMap<String, PropertyValuesHolder> mValuesMap;

    public static void setDurationScale(float durationScale) {
        sDurationScale = durationScale;
    }

    public static float getDurationScale() {
        return sDurationScale;
    }

    public static boolean areAnimatorsEnabled() {
        return !(sDurationScale == 0);
    }

    public ValueAnimator() {}

    public static ValueAnimator ofInt(int... values) {
        ValueAnimator anim = new ValueAnimator();
        anim.setIntValues(values);
        return anim;
    }

    public static ValueAnimator ofArgb(int... values) {
        ValueAnimator anim = new ValueAnimator();
        anim.setIntValues(values);
        anim.setEvaluator(ArgbEvaluator.getInstance());
        return anim;
    }

    public static ValueAnimator ofFloat(float... values) {
        ValueAnimator anim = new ValueAnimator();
        anim.setFloatValues(values);
        return anim;
    }

    public static ValueAnimator ofPropertyValuesHolder(PropertyValuesHolder... values) {
        ValueAnimator anim = new ValueAnimator();
        anim.setValues(values);
        return anim;
    }

    public static ValueAnimator ofObject(TypeEvaluator evaluator, Object... values) {
        ValueAnimator anim = new ValueAnimator();
        anim.setObjectValues(values);
        anim.setEvaluator(evaluator);
        return anim;
    }

    public void setIntValues(int... values) {
        if (values == null || values.length == 0) return;
        if (mValues == null || mValues.length == 0) {
            setValues(PropertyValuesHolder.ofInt("", values));
        } else {
            mValues[0].setIntValues(values);
        }
        mInitialized = false;
    }

    public void setFloatValues(float... values) {
        if (values == null || values.length == 0) return;
        if (mValues == null || mValues.length == 0) {
            setValues(PropertyValuesHolder.ofFloat("", values));
        } else {
            mValues[0].setFloatValues(values);
        }
        mInitialized = false;
    }

    public void setObjectValues(Object... values) {
        if (values == null || values.length == 0) return;
        if (mValues == null || mValues.length == 0) {
            setValues(PropertyValuesHolder.ofObject("", null, values));
        } else {
            mValues[0].setObjectValues(values);
        }
        mInitialized = false;
    }

    public void setValues(PropertyValuesHolder... values) {
        mValues = values;
        mValuesMap = new HashMap<String, PropertyValuesHolder>(values.length);
        for (PropertyValuesHolder valuesHolder : values) mValuesMap.put(valuesHolder.getPropertyName(), valuesHolder);
        mInitialized = false;
    }

    public PropertyValuesHolder[] getValues() {
        return mValues;
    }

    void initAnimation() {
        if (!mInitialized) {
            if (mValues != null) {
                for (PropertyValuesHolder v : mValues) v.init();
            }
            mInitialized = true;
        }
    }

    @Override
    public ValueAnimator setDuration(long duration) {
        if (duration < 0) throw new IllegalArgumentException("Animators cannot have negative duration: " + duration);
        mDuration = duration;
        return this;
    }

    private long getScaledDuration() {
        return (long) (mDuration * sDurationScale);
    }

    @Override
    public long getDuration() {
        return mDuration;
    }

    @Override
    public long getTotalDuration() {
        if (mRepeatCount == INFINITE) return DURATION_INFINITE;
        return mStartDelay + (mDuration * (mRepeatCount + 1));
    }

    public void setCurrentPlayTime(long playTime) {
        float fraction = mDuration > 0 ? (float) playTime / mDuration : 1;
        setCurrentFraction(fraction);
    }

    public void setCurrentFraction(float fraction) {
        initAnimation();
        fraction = clampFraction(fraction);
        mStartListenersCalled = true;
        if (isPulsingInternal()) {
            long seekTime = (long) (getScaledDuration() * fraction);
            long currentTime = AnimationUtils.currentAnimationTimeMillis();
            mStartTime = currentTime - seekTime;
        } else {
            mSeekFraction = fraction;
        }
        mOverallFraction = fraction;
        final float currentIterationFraction = getCurrentIterationFraction(fraction, mReversing);
        animateValue(currentIterationFraction);
    }

    private boolean isPulsingInternal() {
        return mLastFrameTime >= 0;
    }

    private int getCurrentIteration(float fraction) {
        fraction = clampFraction(fraction);
        double iteration = Math.floor(fraction);
        if (fraction == iteration && fraction > 0) iteration--;
        return (int) iteration;
    }

    private float getCurrentIterationFraction(float fraction, boolean inReverse) {
        fraction = clampFraction(fraction);
        int iteration = getCurrentIteration(fraction);
        float currentFraction = fraction - iteration;
        return shouldPlayBackward(iteration, inReverse) ? 1f - currentFraction : currentFraction;
    }

    private float clampFraction(float fraction) {
        if (fraction < 0) {
            fraction = 0;
        } else if (mRepeatCount != INFINITE) {
            fraction = Math.min(fraction, mRepeatCount + 1);
        }
        return fraction;
    }

    private boolean shouldPlayBackward(int iteration, boolean inReverse) {
        if (iteration > 0 && mRepeatMode == REVERSE && (iteration < (mRepeatCount + 1) || mRepeatCount == INFINITE)) {
            if (inReverse) return (iteration % 2) == 0;
            return (iteration % 2) != 0;
        }
        return inReverse;
    }

    public long getCurrentPlayTime() {
        if (!mInitialized || (!mStarted && mSeekFraction < 0)) return 0;
        if (mSeekFraction >= 0) return (long) (mDuration * mSeekFraction);
        float durationScale = sDurationScale == 0 ? 1 : sDurationScale;
        return (long) ((AnimationUtils.currentAnimationTimeMillis() - mStartTime) / durationScale);
    }

    @Override
    public long getStartDelay() {
        return mStartDelay;
    }

    @Override
    public void setStartDelay(long startDelay) {
        if (startDelay < 0) startDelay = 0;
        mStartDelay = startDelay;
    }

    public static long getFrameDelay() {
        return 10;
    }

    public static void setFrameDelay(long frameDelay) {}

    public Object getAnimatedValue() {
        if (mValues != null && mValues.length > 0) return mValues[0].getAnimatedValue();
        return null;
    }

    public Object getAnimatedValue(String propertyName) {
        PropertyValuesHolder valuesHolder = mValuesMap.get(propertyName);
        return valuesHolder != null ? valuesHolder.getAnimatedValue() : null;
    }

    public void setRepeatCount(int value) {
        mRepeatCount = value;
    }

    public int getRepeatCount() {
        return mRepeatCount;
    }

    public void setRepeatMode(int value) {
        mRepeatMode = value;
    }

    public int getRepeatMode() {
        return mRepeatMode;
    }

    public void addUpdateListener(AnimatorUpdateListener listener) {
        if (mUpdateListeners == null) mUpdateListeners = new ArrayList<AnimatorUpdateListener>();
        mUpdateListeners.add(listener);
    }

    public void removeAllUpdateListeners() {
        if (mUpdateListeners == null) return;
        mUpdateListeners.clear();
        mUpdateListeners = null;
    }

    public void removeUpdateListener(AnimatorUpdateListener listener) {
        if (mUpdateListeners == null) return;
        mUpdateListeners.remove(listener);
        if (mUpdateListeners.size() == 0) mUpdateListeners = null;
    }

    @Override
    public void setInterpolator(TimeInterpolator value) {
        mInterpolator = value != null ? value : new LinearInterpolator();
    }

    @Override
    public TimeInterpolator getInterpolator() {
        return mInterpolator;
    }

    public void setEvaluator(TypeEvaluator value) {
        if (value != null && mValues != null && mValues.length > 0) mValues[0].setEvaluator(value);
    }

    private void notifyStartListeners() {
        if (mListeners != null && !mStartListenersCalled) {
            for (AnimatorListener l : listenersCopy()) l.onAnimationStart(this, mReversing);
        }
        mStartListenersCalled = true;
    }

    private void start(boolean playBackwards) {
        if (Looper.myLooper() == null) {
            throw new android.util.AndroidRuntimeException("Animators may only be run on Looper threads");
        }
        mReversing = playBackwards;
        if (playBackwards && mSeekFraction != -1 && mSeekFraction != 0) {
            if (mRepeatCount == INFINITE) {
                float fraction = (float) (mSeekFraction - Math.floor(mSeekFraction));
                mSeekFraction = 1 - fraction;
            } else {
                mSeekFraction = 1 + mRepeatCount - mSeekFraction;
            }
        }
        mStarted = true;
        mPaused = false;
        mRunning = false;
        mAnimationEndRequested = false;
        mLastFrameTime = -1;
        mStartTime = -1;
        AnimationHandler.getInstance().addAnimationFrameCallback(this);
        if (mStartDelay == 0 || mSeekFraction >= 0 || mReversing) {
            startAnimation();
            if (mSeekFraction == -1) {
                setCurrentPlayTime(0);
            } else {
                setCurrentFraction(mSeekFraction);
            }
        }
    }

    @Override
    public void start() {
        start(false);
    }

    @Override
    public void cancel() {
        if (mAnimationEndRequested) return;
        if ((mStarted || mRunning) && mListeners != null) {
            if (!mRunning) notifyStartListeners();
            for (AnimatorListener l : listenersCopy()) l.onAnimationCancel(this);
        }
        endAnimation();
    }

    @Override
    public void end() {
        if (!mRunning) {
            startAnimation();
            mStarted = true;
        } else if (!mInitialized) {
            initAnimation();
        }
        animateValue(shouldPlayBackward(mRepeatCount, mReversing) ? 0f : 1f);
        endAnimation();
    }

    @Override
    public void resume() {
        if (mPaused && !mResumed) {
            mResumed = true;
            if (mPauseTime > 0) AnimationHandler.getInstance().addAnimationFrameCallback(this);
        }
        super.resume();
    }

    @Override
    public void pause() {
        boolean previouslyPaused = mPaused;
        super.pause();
        if (!previouslyPaused && mPaused) {
            mPauseTime = -1;
            mResumed = false;
        }
    }

    @Override
    public boolean isRunning() {
        return mRunning;
    }

    @Override
    public boolean isStarted() {
        return mStarted;
    }

    public void reverse() {
        if (isPulsingInternal()) {
            long currentTime = AnimationUtils.currentAnimationTimeMillis();
            long currentPlayTime = currentTime - mStartTime;
            long timeLeft = getScaledDuration() - currentPlayTime;
            mStartTime = currentTime - timeLeft;
            mReversing = !mReversing;
        } else if (mStarted) {
            mReversing = !mReversing;
            end();
        } else {
            start(true);
        }
    }

    private void endAnimation() {
        if (mAnimationEndRequested) return;
        AnimationHandler.getInstance().removeCallback(this);
        mAnimationEndRequested = true;
        mPaused = false;
        boolean notify = (mStarted || mRunning) && mListeners != null;
        if (notify && !mRunning) notifyStartListeners();
        mRunning = false;
        mStarted = false;
        mStartListenersCalled = false;
        mLastFrameTime = -1;
        mStartTime = -1;
        if (notify && mListeners != null) {
            for (AnimatorListener l : listenersCopy()) l.onAnimationEnd(this, mReversing);
        }
        mReversing = false;
    }

    private void startAnimation() {
        mAnimationEndRequested = false;
        initAnimation();
        mRunning = true;
        if (mSeekFraction >= 0) {
            mOverallFraction = mSeekFraction;
        } else {
            mOverallFraction = 0f;
        }
        if (mListeners != null) notifyStartListeners();
    }

    boolean animateBasedOnTime(long currentTime) {
        boolean done = false;
        if (mRunning) {
            final long scaledDuration = getScaledDuration();
            final float fraction = scaledDuration > 0 ? (float) (currentTime - mStartTime) / scaledDuration : 1f;
            final float lastFraction = mOverallFraction;
            final boolean newIteration = (int) fraction > (int) lastFraction;
            final boolean lastIterationFinished = (fraction >= mRepeatCount + 1) && (mRepeatCount != INFINITE);
            if (scaledDuration == 0) {
                done = true;
            } else if (newIteration && !lastIterationFinished) {
                if (mListeners != null) {
                    for (AnimatorListener l : listenersCopy()) l.onAnimationRepeat(this);
                }
            } else if (lastIterationFinished) {
                done = true;
            }
            mOverallFraction = clampFraction(fraction);
            float currentIterationFraction = getCurrentIterationFraction(mOverallFraction, mReversing);
            animateValue(currentIterationFraction);
        }
        return done;
    }

    @Override
    public final boolean doAnimationFrame(long frameTime) {
        if (mStartTime < 0) {
            mStartTime = mReversing ? frameTime : frameTime + (long) (mStartDelay * sDurationScale);
        }
        if (mPaused) {
            mPauseTime = frameTime;
            AnimationHandler.getInstance().removeCallback(this);
            return false;
        } else if (mResumed) {
            mResumed = false;
            if (mPauseTime > 0) mStartTime += (frameTime - mPauseTime);
        }
        if (!mRunning) {
            if (mStartTime > frameTime && mSeekFraction == -1) return false;
            mRunning = true;
            startAnimation();
        }
        if (mLastFrameTime < 0) {
            if (mSeekFraction >= 0) {
                long seekTime = (long) (getScaledDuration() * mSeekFraction);
                mStartTime = frameTime - seekTime;
                mSeekFraction = -1;
            }
        }
        mLastFrameTime = frameTime;
        final long currentTime = Math.max(frameTime, mStartTime);
        boolean finished = animateBasedOnTime(currentTime);
        if (finished) endAnimation();
        return finished;
    }

    public float getAnimatedFraction() {
        return mCurrentFraction;
    }

    void animateValue(float fraction) {
        fraction = mInterpolator.getInterpolation(fraction);
        mCurrentFraction = fraction;
        if (mValues != null) {
            for (PropertyValuesHolder v : mValues) v.calculateValue(fraction);
        }
        if (mUpdateListeners != null) {
            for (AnimatorUpdateListener l : new ArrayList<AnimatorUpdateListener>(mUpdateListeners)) {
                l.onAnimationUpdate(this);
            }
        }
    }

    @Override
    public ValueAnimator clone() {
        final ValueAnimator anim = (ValueAnimator) super.clone();
        if (mUpdateListeners != null) anim.mUpdateListeners = new ArrayList<AnimatorUpdateListener>(mUpdateListeners);
        anim.mSeekFraction = -1;
        anim.mReversing = false;
        anim.mInitialized = false;
        anim.mStarted = false;
        anim.mRunning = false;
        anim.mPaused = false;
        anim.mResumed = false;
        anim.mStartListenersCalled = false;
        anim.mStartTime = -1;
        anim.mAnimationEndRequested = false;
        anim.mPauseTime = -1;
        anim.mLastFrameTime = -1;
        anim.mOverallFraction = 0;
        anim.mCurrentFraction = 0;
        PropertyValuesHolder[] oldValues = mValues;
        if (oldValues != null) {
            int numValues = oldValues.length;
            anim.mValues = new PropertyValuesHolder[numValues];
            anim.mValuesMap = new HashMap<String, PropertyValuesHolder>(numValues);
            for (int i = 0; i < numValues; ++i) {
                PropertyValuesHolder newValuesHolder = oldValues[i].clone();
                anim.mValues[i] = newValuesHolder;
                anim.mValuesMap.put(newValuesHolder.getPropertyName(), newValuesHolder);
            }
        }
        return anim;
    }

    @Override
    public String toString() {
        String returnVal = "ValueAnimator@" + Integer.toHexString(hashCode());
        if (mValues != null) {
            for (PropertyValuesHolder v : mValues) returnVal += "\n    " + v.toString();
        }
        return returnVal;
    }
}
