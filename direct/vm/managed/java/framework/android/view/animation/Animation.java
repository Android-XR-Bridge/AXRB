package android.view.animation;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.RectF;
import android.os.Handler;
import android.util.AttributeSet;
import android.util.TypedValue;

/** A view animation: a timed transformation (alpha and matrix) applied while a view draws. */
public abstract class Animation implements Cloneable {
    public static final int INFINITE = -1;
    public static final int RESTART = 1;
    public static final int REVERSE = 2;
    public static final int START_ON_FIRST_FRAME = -1;
    public static final int ABSOLUTE = 0;
    public static final int RELATIVE_TO_SELF = 1;
    public static final int RELATIVE_TO_PARENT = 2;
    public static final int ZORDER_NORMAL = 0;
    public static final int ZORDER_TOP = 1;
    public static final int ZORDER_BOTTOM = -1;

    public interface AnimationListener {
        void onAnimationStart(Animation animation);

        void onAnimationEnd(Animation animation);

        void onAnimationRepeat(Animation animation);
    }

    boolean mEnded = false;
    boolean mStarted = false;
    boolean mCycleFlip = false;
    boolean mInitialized = false;
    boolean mFillBefore = true;
    boolean mFillAfter = false;
    boolean mFillEnabled = false;
    long mStartTime = -1;
    long mStartOffset;
    long mDuration;
    int mRepeatCount = 0;
    int mRepeated = 0;
    int mRepeatMode = RESTART;
    Interpolator mInterpolator;
    AnimationListener mListener;
    private int mZAdjustment;
    private int mBackgroundColor;
    private float mScaleFactor = 1f;
    private boolean mMore = true;
    private boolean mOneMoreTime = true;
    private Handler mListenerHandler;
    RectF mPreviousRegion = new RectF();
    RectF mRegion = new RectF();
    Transformation mTransformation = new Transformation();
    Transformation mPreviousTransformation = new Transformation();

    public Animation() {
        ensureInterpolator();
    }

    public Animation(Context context, AttributeSet attrs) {
        TypedArray a = context.obtainStyledAttributes(attrs, android.R.styleable.Animation);
        setDuration((long) a.getInt(android.R.styleable.Animation_duration, 0));
        setStartOffset((long) a.getInt(android.R.styleable.Animation_startOffset, 0));
        setFillEnabled(a.getBoolean(android.R.styleable.Animation_fillEnabled, mFillEnabled));
        setFillBefore(a.getBoolean(android.R.styleable.Animation_fillBefore, mFillBefore));
        setFillAfter(a.getBoolean(android.R.styleable.Animation_fillAfter, mFillAfter));
        setRepeatCount(a.getInt(android.R.styleable.Animation_repeatCount, mRepeatCount));
        setRepeatMode(a.getInt(android.R.styleable.Animation_repeatMode, RESTART));
        final int resID = a.getResourceId(android.R.styleable.Animation_interpolator, 0);
        a.recycle();
        if (resID > 0) setInterpolator(context, resID);
        ensureInterpolator();
    }

    @Override
    protected Animation clone() throws CloneNotSupportedException {
        final Animation animation = (Animation) super.clone();
        animation.mPreviousRegion = new RectF();
        animation.mRegion = new RectF();
        animation.mTransformation = new Transformation();
        animation.mPreviousTransformation = new Transformation();
        return animation;
    }

    public void reset() {
        mPreviousRegion.setEmpty();
        mPreviousTransformation.clear();
        mInitialized = false;
        mCycleFlip = false;
        mRepeated = 0;
        mMore = true;
        mOneMoreTime = true;
        mListenerHandler = null;
    }

    public void cancel() {
        if (mStarted && !mEnded) {
            fireAnimationEnd();
            mEnded = true;
        }
        mStartTime = Long.MIN_VALUE;
        mMore = mOneMoreTime = false;
    }

    public void detach() {
        if (mStarted && !mEnded) {
            mEnded = true;
            fireAnimationEnd();
        }
    }

    public boolean isInitialized() {
        return mInitialized;
    }

    public void initialize(int width, int height, int parentWidth, int parentHeight) {
        reset();
        mInitialized = true;
    }

    public void setListenerHandler(Handler handler) {
        mListenerHandler = handler;
    }

    public void setInterpolator(Context context, int resID) {
        setInterpolator(AnimationUtils.loadInterpolator(context, resID));
    }

    public void setInterpolator(Interpolator i) {
        mInterpolator = i;
    }

    public void setStartOffset(long startOffset) {
        mStartOffset = startOffset;
    }

    public void setDuration(long durationMillis) {
        if (durationMillis < 0) throw new IllegalArgumentException("Animation duration cannot be negative");
        mDuration = durationMillis;
    }

    public void restrictDuration(long durationMillis) {
        if (mStartOffset > durationMillis) {
            mStartOffset = durationMillis;
            mDuration = 0;
            mRepeatCount = 0;
            return;
        }
        long dur = mDuration + mStartOffset;
        if (dur > durationMillis) {
            mDuration = durationMillis - mStartOffset;
            dur = durationMillis;
        }
        if (mDuration <= 0) {
            mDuration = 0;
            mRepeatCount = 0;
            return;
        }
        if (mRepeatCount < 0 || mRepeatCount > durationMillis || (dur * mRepeatCount) > durationMillis) {
            mRepeatCount = (int) (durationMillis / dur) - 1;
            if (mRepeatCount < 0) mRepeatCount = 0;
        }
    }

    public void scaleCurrentDuration(float scale) {
        mDuration = (long) (mDuration * scale);
        mStartOffset = (long) (mStartOffset * scale);
    }

    public void setStartTime(long startTimeMillis) {
        mStartTime = startTimeMillis;
        mStarted = mEnded = false;
        mCycleFlip = false;
        mRepeated = 0;
        mMore = true;
    }

    public void start() {
        setStartTime(-1);
    }

    public void startNow() {
        setStartTime(AnimationUtils.currentAnimationTimeMillis());
    }

    public void setRepeatMode(int repeatMode) {
        mRepeatMode = repeatMode;
    }

    public void setRepeatCount(int repeatCount) {
        if (repeatCount < 0) repeatCount = INFINITE;
        mRepeatCount = repeatCount;
    }

    public boolean isFillEnabled() {
        return mFillEnabled;
    }

    public void setFillEnabled(boolean fillEnabled) {
        mFillEnabled = fillEnabled;
    }

    public void setFillBefore(boolean fillBefore) {
        mFillBefore = fillBefore;
    }

    public void setFillAfter(boolean fillAfter) {
        mFillAfter = fillAfter;
    }

    public void setZAdjustment(int zAdjustment) {
        mZAdjustment = zAdjustment;
    }

    public void setBackgroundColor(int bg) {
        mBackgroundColor = bg;
    }

    protected float getScaleFactor() {
        return mScaleFactor;
    }

    @Deprecated
    public void setDetachWallpaper(boolean detachWallpaper) {}

    public Interpolator getInterpolator() {
        return mInterpolator;
    }

    public long getStartTime() {
        return mStartTime;
    }

    public long getDuration() {
        return mDuration;
    }

    public long getStartOffset() {
        return mStartOffset;
    }

    public int getRepeatMode() {
        return mRepeatMode;
    }

    public int getRepeatCount() {
        return mRepeatCount;
    }

    public boolean getFillBefore() {
        return mFillBefore;
    }

    public boolean getFillAfter() {
        return mFillAfter;
    }

    public int getZAdjustment() {
        return mZAdjustment;
    }

    public int getBackgroundColor() {
        return mBackgroundColor;
    }

    public boolean willChangeTransformationMatrix() {
        return true;
    }

    public boolean willChangeBounds() {
        return true;
    }

    public void setAnimationListener(AnimationListener listener) {
        mListener = listener;
    }

    protected void ensureInterpolator() {
        if (mInterpolator == null) mInterpolator = new AccelerateDecelerateInterpolator();
    }

    public long computeDurationHint() {
        return (getStartOffset() + getDuration()) * (getRepeatCount() + 1);
    }

    public boolean getTransformation(long currentTime, Transformation outTransformation) {
        if (mStartTime == -1) mStartTime = currentTime;
        final long startOffset = getStartOffset();
        final long duration = mDuration;
        float normalizedTime;
        if (duration != 0) {
            normalizedTime = ((float) (currentTime - (mStartTime + startOffset))) / (float) duration;
        } else {
            normalizedTime = currentTime < mStartTime ? 0.0f : 1.0f;
        }
        final boolean expired = normalizedTime >= 1.0f || isCanceled();
        mMore = !expired;
        if (!mFillEnabled) normalizedTime = Math.max(Math.min(normalizedTime, 1.0f), 0.0f);
        if ((normalizedTime >= 0.0f || mFillBefore) && (normalizedTime <= 1.0f || mFillAfter)) {
            if (!mStarted) {
                fireAnimationStart();
                mStarted = true;
            }
            if (mFillEnabled) normalizedTime = Math.max(Math.min(normalizedTime, 1.0f), 0.0f);
            if (mCycleFlip) normalizedTime = 1.0f - normalizedTime;
            final float interpolatedTime = mInterpolator.getInterpolation(normalizedTime);
            applyTransformation(interpolatedTime, outTransformation);
        }
        if (expired) {
            if (mRepeatCount == mRepeated || isCanceled()) {
                if (!mEnded) {
                    mEnded = true;
                    fireAnimationEnd();
                }
            } else {
                if (mRepeatCount > 0) mRepeated++;
                if (mRepeatMode == REVERSE) mCycleFlip = !mCycleFlip;
                mStartTime = -1;
                mMore = true;
                fireAnimationRepeat();
            }
        }
        if (!mMore && mOneMoreTime) {
            mOneMoreTime = false;
            return true;
        }
        return mMore;
    }

    private boolean isCanceled() {
        return mStartTime == Long.MIN_VALUE;
    }

    private void fireAnimationStart() {
        if (mListener != null) {
            if (mListenerHandler == null) {
                mListener.onAnimationStart(this);
            } else {
                mListenerHandler.postAtFrontOfQueue(new Runnable() {
                    public void run() {
                        if (mListener != null) mListener.onAnimationStart(Animation.this);
                    }
                });
            }
        }
    }

    private void fireAnimationRepeat() {
        if (mListener != null) {
            if (mListenerHandler == null) {
                mListener.onAnimationRepeat(this);
            } else {
                mListenerHandler.postAtFrontOfQueue(new Runnable() {
                    public void run() {
                        if (mListener != null) mListener.onAnimationRepeat(Animation.this);
                    }
                });
            }
        }
    }

    private void fireAnimationEnd() {
        if (mListener != null) {
            if (mListenerHandler == null) {
                mListener.onAnimationEnd(this);
            } else {
                mListenerHandler.postAtFrontOfQueue(new Runnable() {
                    public void run() {
                        if (mListener != null) mListener.onAnimationEnd(Animation.this);
                    }
                });
            }
        }
    }

    public boolean getTransformation(long currentTime, Transformation outTransformation, float scale) {
        mScaleFactor = scale;
        return getTransformation(currentTime, outTransformation);
    }

    public boolean hasStarted() {
        return mStarted;
    }

    public boolean hasEnded() {
        return mEnded;
    }

    protected void applyTransformation(float interpolatedTime, Transformation t) {}

    protected float resolveSize(int type, float value, int size, int parentSize) {
        switch (type) {
            case ABSOLUTE:
                return value;
            case RELATIVE_TO_SELF:
                return size * value;
            case RELATIVE_TO_PARENT:
                return parentSize * value;
            default:
                return value;
        }
    }

    public boolean hasAlpha() {
        return false;
    }

    /** A size or position from XML: absolute, "n%" of the view, or "n%p" of the parent. */
    protected static class Description {
        public int type;
        public float value;

        static Description parseValue(TypedValue value) {
            Description d = new Description();
            if (value == null) {
                d.type = ABSOLUTE;
                d.value = 0;
            } else {
                if (value.type == TypedValue.TYPE_FRACTION) {
                    d.type = (value.data & TypedValue.COMPLEX_UNIT_MASK) == TypedValue.COMPLEX_UNIT_FRACTION_PARENT
                            ? RELATIVE_TO_PARENT : RELATIVE_TO_SELF;
                    d.value = TypedValue.complexToFloat(value.data);
                    return d;
                } else if (value.type == TypedValue.TYPE_FLOAT) {
                    d.type = ABSOLUTE;
                    d.value = value.getFloat();
                    return d;
                } else if (value.type >= TypedValue.TYPE_FIRST_INT && value.type <= TypedValue.TYPE_LAST_INT) {
                    d.type = ABSOLUTE;
                    d.value = value.data;
                    return d;
                } else if (value.type == TypedValue.TYPE_DIMENSION) {
                    d.type = ABSOLUTE;
                    d.value = TypedValue.complexToDimension(value.data,
                            android.content.res.Resources.getSystem().getDisplayMetrics());
                    return d;
                }
            }
            d.type = ABSOLUTE;
            d.value = 0.0f;
            return d;
        }
    }
}
