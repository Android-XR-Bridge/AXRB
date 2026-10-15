package android.animation;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/**
 * Plays animators together or in sequence. Each child starts when every
 * animator it plays after has ended; the set ends when all children ended.
 */
public final class AnimatorSet extends Animator {
    private static final class Node {
        final Animator mAnimation;
        final ArrayList<Node> mDependencies = new ArrayList<Node>();
        final ArrayList<Node> mSiblings = new ArrayList<Node>();
        boolean mStarted;
        boolean mEnded;

        Node(Animator animation) {
            mAnimation = animation;
        }
    }

    private ArrayList<Node> mNodes = new ArrayList<Node>();
    private long mStartDelay = 0;
    private long mDuration = -1;
    private TimeInterpolator mInterpolator;
    private boolean mStarted;
    private boolean mRunning;
    private boolean mCanceling;
    private ValueAnimator mDelayAnim;

    public AnimatorSet() {}

    public void playTogether(Animator... items) {
        if (items != null) {
            Builder builder = play(items[0]);
            for (int i = 1; i < items.length; ++i) builder.with(items[i]);
        }
    }

    public void playTogether(Collection<Animator> items) {
        if (items != null && items.size() > 0) {
            Builder builder = null;
            for (Animator anim : items) {
                if (builder == null) {
                    builder = play(anim);
                } else {
                    builder.with(anim);
                }
            }
        }
    }

    public void playSequentially(Animator... items) {
        if (items != null) {
            if (items.length == 1) {
                play(items[0]);
            } else {
                for (int i = 0; i < items.length - 1; ++i) play(items[i]).before(items[i + 1]);
            }
        }
    }

    public void playSequentially(List<Animator> items) {
        if (items != null && items.size() > 0) {
            if (items.size() == 1) {
                play(items.get(0));
            } else {
                for (int i = 0; i < items.size() - 1; ++i) play(items.get(i)).before(items.get(i + 1));
            }
        }
    }

    public ArrayList<Animator> getChildAnimations() {
        ArrayList<Animator> childList = new ArrayList<Animator>();
        for (Node n : mNodes) childList.add(n.mAnimation);
        return childList;
    }

    @Override
    public void setTarget(Object target) {
        for (Node n : mNodes) n.mAnimation.setTarget(target);
    }

    @Override
    public void setInterpolator(TimeInterpolator interpolator) {
        mInterpolator = interpolator;
    }

    @Override
    public TimeInterpolator getInterpolator() {
        return mInterpolator;
    }

    public Builder play(Animator anim) {
        return anim != null ? new Builder(anim) : null;
    }

    private Node getNodeForAnimation(Animator anim) {
        for (Node n : mNodes) {
            if (n.mAnimation == anim) return n;
        }
        Node node = new Node(anim);
        mNodes.add(node);
        return node;
    }

    @Override
    public void cancel() {
        if (!mStarted) return;
        mCanceling = true;
        ArrayList<AnimatorListener> listeners = listenersCopy();
        if (listeners != null) {
            for (AnimatorListener l : listeners) l.onAnimationCancel(this);
        }
        if (mDelayAnim != null && mDelayAnim.isStarted()) mDelayAnim.cancel();
        for (Node n : new ArrayList<Node>(mNodes)) {
            if (n.mStarted && !n.mEnded) n.mAnimation.cancel();
        }
        finish();
        mCanceling = false;
    }

    @Override
    public void end() {
        if (!mStarted) return;
        if (mDelayAnim != null && mDelayAnim.isStarted()) mDelayAnim.cancel();
        // Start and end every child that has not ended, in dependency order.
        boolean progress = true;
        while (progress) {
            progress = false;
            for (Node n : new ArrayList<Node>(mNodes)) {
                if (n.mEnded) continue;
                if (!n.mStarted && !dependenciesEnded(n)) continue;
                if (!n.mStarted) startNode(n);
                n.mAnimation.end();
                n.mEnded = true;
                progress = true;
            }
        }
        if (mStarted) finish();
    }

    @Override
    public boolean isRunning() {
        return mRunning;
    }

    @Override
    public boolean isStarted() {
        return mStarted;
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

    @Override
    public long getDuration() {
        return mDuration;
    }

    @Override
    public AnimatorSet setDuration(long duration) {
        if (duration < 0) throw new IllegalArgumentException("duration must be a value of zero or greater");
        mDuration = duration;
        return this;
    }

    @Override
    public void setupStartValues() {
        for (Node n : mNodes) n.mAnimation.setupStartValues();
    }

    @Override
    public void setupEndValues() {
        for (Node n : mNodes) n.mAnimation.setupEndValues();
    }

    @Override
    public void start() {
        if (mStarted) return;
        mStarted = true;
        mCanceling = false;
        for (Node n : mNodes) {
            n.mStarted = false;
            n.mEnded = false;
            if (mDuration >= 0) n.mAnimation.setDuration(mDuration);
            if (mInterpolator != null) n.mAnimation.setInterpolator(mInterpolator);
        }
        ArrayList<AnimatorListener> listeners = listenersCopy();
        if (listeners != null) {
            for (AnimatorListener l : listeners) l.onAnimationStart(this, false);
        }
        if (mNodes.isEmpty()) {
            finish();
            return;
        }
        if (mStartDelay > 0) {
            mDelayAnim = ValueAnimator.ofFloat(0f, 1f);
            mDelayAnim.setDuration(mStartDelay);
            mDelayAnim.addListener(new AnimatorListenerAdapter() {
                @Override
                public void onAnimationEnd(Animator anim) {
                    if (!mCanceling && mStarted) startReadyNodes();
                }
            });
            mDelayAnim.start();
        } else {
            startReadyNodes();
        }
    }

    private boolean dependenciesEnded(Node node) {
        for (Node d : node.mDependencies) {
            if (!d.mEnded) return false;
        }
        return true;
    }

    private void startReadyNodes() {
        mRunning = true;
        for (Node n : new ArrayList<Node>(mNodes)) {
            if (!n.mStarted && dependenciesEnded(n)) startNode(n);
        }
    }

    private void startNode(final Node node) {
        node.mStarted = true;
        node.mAnimation.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                animation.removeListener(this);
                node.mEnded = true;
                if (mCanceling || !mStarted) return;
                for (Node n : mNodes) {
                    if (!n.mEnded) {
                        startReadyNodes();
                        return;
                    }
                }
                finish();
            }
        });
        node.mAnimation.start();
    }

    private void finish() {
        if (!mStarted) return;
        mStarted = false;
        mRunning = false;
        ArrayList<AnimatorListener> listeners = listenersCopy();
        if (listeners != null) {
            for (AnimatorListener l : listeners) l.onAnimationEnd(this, false);
        }
    }

    @Override
    public AnimatorSet clone() {
        final AnimatorSet anim = (AnimatorSet) super.clone();
        anim.mNodes = new ArrayList<Node>();
        anim.mStarted = false;
        anim.mRunning = false;
        anim.mCanceling = false;
        anim.mDelayAnim = null;
        java.util.HashMap<Node, Node> clones = new java.util.HashMap<Node, Node>();
        for (Node n : mNodes) {
            Node c = new Node(n.mAnimation.clone());
            clones.put(n, c);
            anim.mNodes.add(c);
        }
        for (Node n : mNodes) {
            Node c = clones.get(n);
            for (Node d : n.mDependencies) c.mDependencies.add(clones.get(d));
            for (Node s : n.mSiblings) c.mSiblings.add(clones.get(s));
        }
        return anim;
    }

    @Override
    public String toString() {
        return "AnimatorSet@" + Integer.toHexString(hashCode()) + getChildAnimations();
    }

    /** Declares ordering relative to the animator given to play(). */
    public class Builder {
        private final Node mCurrentNode;

        Builder(Animator anim) {
            mCurrentNode = getNodeForAnimation(anim);
        }

        public Builder with(Animator anim) {
            Node node = getNodeForAnimation(anim);
            node.mDependencies.addAll(mCurrentNode.mDependencies);
            mCurrentNode.mSiblings.add(node);
            return this;
        }

        public Builder before(Animator anim) {
            Node node = getNodeForAnimation(anim);
            node.mDependencies.add(mCurrentNode);
            return this;
        }

        public Builder after(Animator anim) {
            Node node = getNodeForAnimation(anim);
            mCurrentNode.mDependencies.add(node);
            for (Node sibling : mCurrentNode.mSiblings) sibling.mDependencies.add(node);
            return this;
        }

        public Builder after(long delay) {
            ValueAnimator anim = ValueAnimator.ofFloat(0f, 1f);
            anim.setDuration(delay);
            after(anim);
            return this;
        }
    }
}
