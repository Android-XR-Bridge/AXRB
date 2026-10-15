package android.view;

import android.animation.Animator;
import android.animation.TimeInterpolator;
import android.animation.ValueAnimator;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Animates a view's properties with one underlying animator per start(),
 * started on the next animation frame unless start() is called (AOSP semantics).
 */
public class ViewPropertyAnimator {
    static final int NONE = 0x0000;
    static final int TRANSLATION_X = 0x0001;
    static final int TRANSLATION_Y = 0x0002;
    static final int TRANSLATION_Z = 0x0004;
    static final int SCALE_X = 0x0008;
    static final int SCALE_Y = 0x0010;
    static final int ROTATION = 0x0020;
    static final int ROTATION_X = 0x0040;
    static final int ROTATION_Y = 0x0080;
    static final int X = 0x0100;
    static final int Y = 0x0200;
    static final int Z = 0x0400;
    static final int ALPHA = 0x0800;

    private static final class NameValuesHolder {
        final int mNameConstant;
        final float mFromValue;
        final float mDeltaValue;

        NameValuesHolder(int nameConstant, float fromValue, float deltaValue) {
            mNameConstant = nameConstant;
            mFromValue = fromValue;
            mDeltaValue = deltaValue;
        }
    }

    private static final class PropertyBundle {
        int mPropertyMask;
        final ArrayList<NameValuesHolder> mNameValuesHolder;

        PropertyBundle(int propertyMask, ArrayList<NameValuesHolder> nameValuesHolder) {
            mPropertyMask = propertyMask;
            mNameValuesHolder = nameValuesHolder;
        }

        boolean cancel(int propertyConstant) {
            if ((mPropertyMask & propertyConstant) != 0 && mNameValuesHolder != null) {
                for (int i = 0; i < mNameValuesHolder.size(); ++i) {
                    if (mNameValuesHolder.get(i).mNameConstant == propertyConstant) {
                        mNameValuesHolder.remove(i);
                        mPropertyMask &= ~propertyConstant;
                        return true;
                    }
                }
            }
            return false;
        }
    }

    final View mView;
    private long mDuration;
    private boolean mDurationSet = false;
    private long mStartDelay = 0;
    private boolean mStartDelaySet = false;
    private TimeInterpolator mInterpolator;
    private boolean mInterpolatorSet = false;
    private Animator.AnimatorListener mListener = null;
    private ValueAnimator.AnimatorUpdateListener mUpdateListener;
    private final AnimatorEventListener mAnimatorEventListener = new AnimatorEventListener();
    private ArrayList<NameValuesHolder> mPendingAnimations = new ArrayList<NameValuesHolder>();
    private Runnable mPendingStartAction;
    private Runnable mPendingEndAction;
    private final HashMap<Animator, PropertyBundle> mAnimatorMap = new HashMap<Animator, PropertyBundle>();
    private final HashMap<Animator, Runnable> mAnimatorStartActions = new HashMap<Animator, Runnable>();
    private final HashMap<Animator, Runnable> mAnimatorEndActions = new HashMap<Animator, Runnable>();

    private final Runnable mAnimationStarter = new Runnable() {
        @Override
        public void run() {
            startAnimation();
        }
    };

    ViewPropertyAnimator(View view) {
        mView = view;
    }

    public ViewPropertyAnimator setDuration(long duration) {
        if (duration < 0) throw new IllegalArgumentException("Animators cannot have negative duration: " + duration);
        mDurationSet = true;
        mDuration = duration;
        return this;
    }

    public long getDuration() {
        if (mDurationSet) return mDuration;
        return new ValueAnimator().getDuration();
    }

    public long getStartDelay() {
        return mStartDelaySet ? mStartDelay : 0;
    }

    public ViewPropertyAnimator setStartDelay(long startDelay) {
        if (startDelay < 0) {
            throw new IllegalArgumentException("Animators cannot have negative start delay: " + startDelay);
        }
        mStartDelaySet = true;
        mStartDelay = startDelay;
        return this;
    }

    public ViewPropertyAnimator setInterpolator(TimeInterpolator interpolator) {
        mInterpolatorSet = true;
        mInterpolator = interpolator;
        return this;
    }

    public TimeInterpolator getInterpolator() {
        if (mInterpolatorSet) return mInterpolator;
        return new ValueAnimator().getInterpolator();
    }

    public ViewPropertyAnimator setListener(Animator.AnimatorListener listener) {
        mListener = listener;
        return this;
    }

    public ViewPropertyAnimator setUpdateListener(ValueAnimator.AnimatorUpdateListener listener) {
        mUpdateListener = listener;
        return this;
    }

    public void start() {
        mView.removeCallbacks(mAnimationStarter);
        startAnimation();
    }

    public void cancel() {
        if (mAnimatorMap.size() > 0) {
            for (Animator runningAnim : new ArrayList<Animator>(mAnimatorMap.keySet())) runningAnim.cancel();
        }
        mPendingAnimations.clear();
        mPendingStartAction = null;
        mPendingEndAction = null;
        mView.removeCallbacks(mAnimationStarter);
    }

    public ViewPropertyAnimator x(float value) {
        animateProperty(X, value);
        return this;
    }

    public ViewPropertyAnimator xBy(float value) {
        animatePropertyBy(X, value);
        return this;
    }

    public ViewPropertyAnimator y(float value) {
        animateProperty(Y, value);
        return this;
    }

    public ViewPropertyAnimator yBy(float value) {
        animatePropertyBy(Y, value);
        return this;
    }

    public ViewPropertyAnimator z(float value) {
        animateProperty(Z, value);
        return this;
    }

    public ViewPropertyAnimator zBy(float value) {
        animatePropertyBy(Z, value);
        return this;
    }

    public ViewPropertyAnimator rotation(float value) {
        animateProperty(ROTATION, value);
        return this;
    }

    public ViewPropertyAnimator rotationBy(float value) {
        animatePropertyBy(ROTATION, value);
        return this;
    }

    public ViewPropertyAnimator rotationX(float value) {
        animateProperty(ROTATION_X, value);
        return this;
    }

    public ViewPropertyAnimator rotationXBy(float value) {
        animatePropertyBy(ROTATION_X, value);
        return this;
    }

    public ViewPropertyAnimator rotationY(float value) {
        animateProperty(ROTATION_Y, value);
        return this;
    }

    public ViewPropertyAnimator rotationYBy(float value) {
        animatePropertyBy(ROTATION_Y, value);
        return this;
    }

    public ViewPropertyAnimator translationX(float value) {
        animateProperty(TRANSLATION_X, value);
        return this;
    }

    public ViewPropertyAnimator translationXBy(float value) {
        animatePropertyBy(TRANSLATION_X, value);
        return this;
    }

    public ViewPropertyAnimator translationY(float value) {
        animateProperty(TRANSLATION_Y, value);
        return this;
    }

    public ViewPropertyAnimator translationYBy(float value) {
        animatePropertyBy(TRANSLATION_Y, value);
        return this;
    }

    public ViewPropertyAnimator translationZ(float value) {
        animateProperty(TRANSLATION_Z, value);
        return this;
    }

    public ViewPropertyAnimator translationZBy(float value) {
        animatePropertyBy(TRANSLATION_Z, value);
        return this;
    }

    public ViewPropertyAnimator scaleX(float value) {
        animateProperty(SCALE_X, value);
        return this;
    }

    public ViewPropertyAnimator scaleXBy(float value) {
        animatePropertyBy(SCALE_X, value);
        return this;
    }

    public ViewPropertyAnimator scaleY(float value) {
        animateProperty(SCALE_Y, value);
        return this;
    }

    public ViewPropertyAnimator scaleYBy(float value) {
        animatePropertyBy(SCALE_Y, value);
        return this;
    }

    public ViewPropertyAnimator alpha(float value) {
        animateProperty(ALPHA, value);
        return this;
    }

    public ViewPropertyAnimator alphaBy(float value) {
        animatePropertyBy(ALPHA, value);
        return this;
    }

    /** Layers are a hardware-rendering optimization; the software renderer draws directly. */
    public ViewPropertyAnimator withLayer() {
        return this;
    }

    public ViewPropertyAnimator withStartAction(Runnable runnable) {
        mPendingStartAction = runnable;
        return this;
    }

    public ViewPropertyAnimator withEndAction(Runnable runnable) {
        mPendingEndAction = runnable;
        return this;
    }

    private void startAnimation() {
        ValueAnimator animator = ValueAnimator.ofFloat(1.0f);
        ArrayList<NameValuesHolder> nameValueList = mPendingAnimations;
        mPendingAnimations = new ArrayList<NameValuesHolder>();
        int propertyMask = 0;
        for (NameValuesHolder h : nameValueList) propertyMask |= h.mNameConstant;
        mAnimatorMap.put(animator, new PropertyBundle(propertyMask, nameValueList));
        if (mPendingStartAction != null) {
            mAnimatorStartActions.put(animator, mPendingStartAction);
            mPendingStartAction = null;
        }
        if (mPendingEndAction != null) {
            mAnimatorEndActions.put(animator, mPendingEndAction);
            mPendingEndAction = null;
        }
        animator.addUpdateListener(mAnimatorEventListener);
        animator.addListener(mAnimatorEventListener);
        if (mStartDelaySet) animator.setStartDelay(mStartDelay);
        if (mDurationSet) animator.setDuration(mDuration);
        if (mInterpolatorSet) animator.setInterpolator(mInterpolator);
        animator.start();
    }

    private void animateProperty(int constantName, float toValue) {
        float fromValue = getValue(constantName);
        float deltaValue = toValue - fromValue;
        animatePropertyBy(constantName, fromValue, deltaValue);
    }

    private void animatePropertyBy(int constantName, float byValue) {
        float fromValue = getValue(constantName);
        animatePropertyBy(constantName, fromValue, byValue);
    }

    private void animatePropertyBy(int constantName, float startValue, float byValue) {
        // A property animated here stops being animated by earlier, still-running animations.
        if (mAnimatorMap.size() > 0) {
            Animator animatorToCancel = null;
            for (Animator runningAnim : mAnimatorMap.keySet()) {
                PropertyBundle bundle = mAnimatorMap.get(runningAnim);
                if (bundle.cancel(constantName)) {
                    if (bundle.mPropertyMask == NONE) {
                        animatorToCancel = runningAnim;
                        break;
                    }
                }
            }
            if (animatorToCancel != null) animatorToCancel.cancel();
        }
        NameValuesHolder nameValuePair = new NameValuesHolder(constantName, startValue, byValue);
        mPendingAnimations.add(nameValuePair);
        mView.removeCallbacks(mAnimationStarter);
        mView.postOnAnimation(mAnimationStarter);
    }

    private void setValue(int propertyConstant, float value) {
        switch (propertyConstant) {
            case TRANSLATION_X:
                mView.setTranslationX(value);
                break;
            case TRANSLATION_Y:
                mView.setTranslationY(value);
                break;
            case TRANSLATION_Z:
                mView.setTranslationZ(value);
                break;
            case ROTATION:
                mView.setRotation(value);
                break;
            case ROTATION_X:
                mView.setRotationX(value);
                break;
            case ROTATION_Y:
                mView.setRotationY(value);
                break;
            case SCALE_X:
                mView.setScaleX(value);
                break;
            case SCALE_Y:
                mView.setScaleY(value);
                break;
            case X:
                mView.setTranslationX(value - mView.mLeft);
                break;
            case Y:
                mView.setTranslationY(value - mView.mTop);
                break;
            case Z:
                mView.setZ(value);
                break;
            case ALPHA:
                mView.setAlpha(value);
                break;
        }
    }

    private float getValue(int propertyConstant) {
        switch (propertyConstant) {
            case TRANSLATION_X:
                return mView.getTranslationX();
            case TRANSLATION_Y:
                return mView.getTranslationY();
            case TRANSLATION_Z:
                return mView.getTranslationZ();
            case ROTATION:
                return mView.getRotation();
            case ROTATION_X:
                return mView.getRotationX();
            case ROTATION_Y:
                return mView.getRotationY();
            case SCALE_X:
                return mView.getScaleX();
            case SCALE_Y:
                return mView.getScaleY();
            case X:
                return mView.mLeft + mView.getTranslationX();
            case Y:
                return mView.mTop + mView.getTranslationY();
            case Z:
                return mView.getZ();
            case ALPHA:
                return mView.getAlpha();
        }
        return 0;
    }

    private class AnimatorEventListener implements Animator.AnimatorListener, ValueAnimator.AnimatorUpdateListener {
        @Override
        public void onAnimationStart(Animator animation) {
            Runnable r = mAnimatorStartActions.remove(animation);
            if (r != null) r.run();
            if (mListener != null) mListener.onAnimationStart(animation);
        }

        @Override
        public void onAnimationCancel(Animator animation) {
            if (mListener != null) mListener.onAnimationCancel(animation);
            mAnimatorEndActions.remove(animation);
        }

        @Override
        public void onAnimationRepeat(Animator animation) {
            if (mListener != null) mListener.onAnimationRepeat(animation);
        }

        @Override
        public void onAnimationEnd(Animator animation) {
            if (mListener != null) mListener.onAnimationEnd(animation);
            Runnable r = mAnimatorEndActions.remove(animation);
            if (r != null) r.run();
            mAnimatorMap.remove(animation);
        }

        @Override
        public void onAnimationUpdate(ValueAnimator animation) {
            PropertyBundle propertyBundle = mAnimatorMap.get(animation);
            if (propertyBundle == null) return;
            float fraction = animation.getAnimatedFraction();
            ArrayList<NameValuesHolder> valueList = propertyBundle.mNameValuesHolder;
            if (valueList != null) {
                for (NameValuesHolder values : valueList) {
                    setValue(values.mNameConstant, values.mFromValue + fraction * values.mDeltaValue);
                }
            }
            mView.invalidate();
            if (mUpdateListener != null) mUpdateListener.onAnimationUpdate(animation);
        }
    }
}
