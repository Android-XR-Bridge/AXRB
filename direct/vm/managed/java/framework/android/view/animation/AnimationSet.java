package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import java.util.ArrayList;
import java.util.List;

/** Animations played together; their transformations compose. */
public class AnimationSet extends Animation {
    private final boolean mShareInterpolator;
    private final ArrayList<Animation> mAnimations = new ArrayList<Animation>();
    private final Transformation mTempTransformation = new Transformation();
    private boolean mHasAlpha;
    private boolean mDurationSet;
    private boolean mFillAfterSet;
    private boolean mFillBeforeSet;
    private boolean mRepeatModeSet;
    private boolean mStartOffsetSet;
    private long mLastEnd;

    public AnimationSet(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.AnimationSet);
        mShareInterpolator = a.getBoolean(android.R.styleable.AnimationSet_shareInterpolator, true);
        mDurationSet = a.hasValue(android.R.styleable.AnimationSet_duration);
        mFillAfterSet = a.hasValue(android.R.styleable.AnimationSet_fillAfter);
        mFillBeforeSet = a.hasValue(android.R.styleable.AnimationSet_fillBefore);
        mRepeatModeSet = a.hasValue(android.R.styleable.AnimationSet_repeatMode);
        mStartOffsetSet = a.hasValue(android.R.styleable.AnimationSet_startOffset);
        a.recycle();
    }

    public AnimationSet(boolean shareInterpolator) {
        mShareInterpolator = shareInterpolator;
    }

    @Override
    public void setFillAfter(boolean fillAfter) {
        mFillAfterSet = true;
        super.setFillAfter(fillAfter);
    }

    @Override
    public void setFillBefore(boolean fillBefore) {
        mFillBeforeSet = true;
        super.setFillBefore(fillBefore);
    }

    @Override
    public void setRepeatMode(int repeatMode) {
        mRepeatModeSet = true;
        super.setRepeatMode(repeatMode);
    }

    @Override
    public void setStartOffset(long startOffset) {
        mStartOffsetSet = true;
        super.setStartOffset(startOffset);
    }

    @Override
    public boolean hasAlpha() {
        return mHasAlpha;
    }

    @Override
    public void setDuration(long durationMillis) {
        mDurationSet = true;
        super.setDuration(durationMillis);
        mLastEnd = mStartOffset + mDuration;
    }

    public void addAnimation(Animation a) {
        mAnimations.add(a);
        if (a.hasAlpha()) mHasAlpha = true;
        if (mAnimations.size() == 1) {
            mDuration = a.getStartOffset() + a.getDuration();
            mLastEnd = mStartOffset + mDuration;
        } else {
            mLastEnd = Math.max(mLastEnd, mStartOffset + a.getStartOffset() + a.getDuration());
            mDuration = mLastEnd - mStartOffset;
        }
    }

    @Override
    public void setStartTime(long startTimeMillis) {
        super.setStartTime(startTimeMillis);
        for (Animation a : mAnimations) a.setStartTime(startTimeMillis);
    }

    @Override
    public long getStartTime() {
        long startTime = Long.MAX_VALUE;
        for (Animation a : mAnimations) startTime = Math.min(startTime, a.getStartTime());
        return startTime;
    }

    @Override
    public void restrictDuration(long durationMillis) {
        super.restrictDuration(durationMillis);
        for (Animation a : mAnimations) a.restrictDuration(durationMillis);
    }

    @Override
    public long getDuration() {
        if (mDurationSet) return mDuration;
        long duration = 0;
        for (Animation a : mAnimations) duration = Math.max(duration, a.getDuration());
        return duration;
    }

    @Override
    public long computeDurationHint() {
        long duration = 0;
        for (Animation a : mAnimations) duration = Math.max(duration, a.computeDurationHint());
        return duration;
    }

    @Override
    public boolean getTransformation(long currentTime, Transformation t) {
        boolean more = false;
        boolean started = false;
        boolean ended = true;
        t.clear();
        for (int i = mAnimations.size() - 1; i >= 0; --i) {
            final Animation a = mAnimations.get(i);
            mTempTransformation.clear();
            more = a.getTransformation(currentTime, mTempTransformation, getScaleFactor()) || more;
            t.compose(mTempTransformation);
            started = started || a.hasStarted();
            ended = a.hasEnded() && ended;
        }
        if (started && !mStarted) {
            if (mListener != null) mListener.onAnimationStart(this);
            mStarted = true;
        }
        if (ended != mEnded) {
            if (mListener != null) mListener.onAnimationEnd(this);
            mEnded = ended;
        }
        return more;
    }

    @Override
    public void scaleCurrentDuration(float scale) {
        for (Animation a : mAnimations) a.scaleCurrentDuration(scale);
    }

    @Override
    public void initialize(int width, int height, int parentWidth, int parentHeight) {
        super.initialize(width, height, parentWidth, parentHeight);
        final boolean durationSet = mDurationSet;
        final boolean fillAfter = mFillAfter;
        final boolean fillBefore = mFillBefore;
        final int repeatMode = mRepeatMode;
        final Interpolator interpolator = mInterpolator;
        final long startOffset = mStartOffset;
        for (Animation a : mAnimations) {
            if (durationSet) a.setDuration(mDuration);
            if (mFillAfterSet) a.setFillAfter(fillAfter);
            if (mFillBeforeSet) a.setFillBefore(fillBefore);
            if (mRepeatModeSet) a.setRepeatMode(repeatMode);
            if (mShareInterpolator) a.setInterpolator(interpolator);
            if (mStartOffsetSet) a.setStartOffset(a.getStartOffset() + startOffset);
            a.initialize(width, height, parentWidth, parentHeight);
        }
    }

    @Override
    public void reset() {
        super.reset();
        for (Animation a : mAnimations) a.reset();
    }

    @Override
    public void cancel() {
        super.cancel();
        for (Animation a : mAnimations) a.cancel();
    }

    public List<Animation> getAnimations() {
        return mAnimations;
    }

    @Override
    public boolean willChangeTransformationMatrix() {
        for (Animation a : mAnimations) {
            if (a.willChangeTransformationMatrix()) return true;
        }
        return false;
    }

    @Override
    public boolean willChangeBounds() {
        for (Animation a : mAnimations) {
            if (a.willChangeBounds()) return true;
        }
        return false;
    }
}
