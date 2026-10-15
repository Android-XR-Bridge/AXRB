package android.view;

/**
 * Pointer velocity from recent samples: a least-squares line fit over the
 * last 100 ms of positions (the "lsq2"-style estimate reduced to degree 1).
 */
public final class VelocityTracker {
    private static final int HISTORY = 20;
    private static final long HORIZON_MS = 100;

    private final long[] mTimes = new long[HISTORY];
    private final float[] mX = new float[HISTORY];
    private final float[] mY = new float[HISTORY];
    private int mCount;
    private int mHead;
    private float mXVelocity;
    private float mYVelocity;

    private VelocityTracker() {}

    public static VelocityTracker obtain() {
        return new VelocityTracker();
    }

    public void recycle() {
        clear();
    }

    public void clear() {
        mCount = 0;
        mHead = 0;
        mXVelocity = 0;
        mYVelocity = 0;
    }

    public void addMovement(MotionEvent event) {
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) clear();
        // Samples are in raw (window) coordinates so that scrolling the view
        // under the pointer does not feed back into the velocity.
        mTimes[mHead] = event.getEventTime();
        mX[mHead] = event.getRawX();
        mY[mHead] = event.getRawY();
        mHead = (mHead + 1) % HISTORY;
        if (mCount < HISTORY) mCount++;
    }

    public void computeCurrentVelocity(int units) {
        computeCurrentVelocity(units, Float.MAX_VALUE);
    }

    public void computeCurrentVelocity(int units, float maxVelocity) {
        mXVelocity = 0;
        mYVelocity = 0;
        if (mCount < 2) return;
        final int newest = (mHead - 1 + HISTORY) % HISTORY;
        final long tEnd = mTimes[newest];
        double st = 0, sx = 0, sy = 0, stt = 0, stx = 0, sty = 0;
        int n = 0;
        for (int k = 0; k < mCount; k++) {
            int i = (newest - k + HISTORY) % HISTORY;
            long age = tEnd - mTimes[i];
            if (age > HORIZON_MS) break;
            double t = -age;
            st += t;
            sx += mX[i];
            sy += mY[i];
            stt += t * t;
            stx += t * mX[i];
            sty += t * mY[i];
            n++;
        }
        if (n < 2) return;
        double denom = n * stt - st * st;
        if (denom == 0) return;
        double vx = (n * stx - st * sx) / denom; // pixels per ms
        double vy = (n * sty - st * sy) / denom;
        mXVelocity = clamp((float) (vx * units), maxVelocity);
        mYVelocity = clamp((float) (vy * units), maxVelocity);
    }

    private static float clamp(float v, float max) {
        if (v > max) return max;
        if (v < -max) return -max;
        return v;
    }

    public float getXVelocity() {
        return mXVelocity;
    }

    public float getYVelocity() {
        return mYVelocity;
    }

    public float getXVelocity(int id) {
        return id == 0 ? mXVelocity : 0;
    }

    public float getYVelocity(int id) {
        return id == 0 ? mYVelocity : 0;
    }
}
