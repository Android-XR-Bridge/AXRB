package android.os;

/** Receives a result code and data from another component, on a handler if one is given. */
public class ResultReceiver implements Parcelable {
    /** The binder a parcelled receiver carries back to the original (in-process). */
    private static final class Link extends Binder {
        final ResultReceiver mOwner;

        Link(ResultReceiver owner) {
            mOwner = owner;
        }
    }

    final Handler mHandler;
    private final ResultReceiver mTarget;
    private Link mLink;

    public ResultReceiver(Handler handler) {
        mHandler = handler;
        mTarget = null;
    }

    ResultReceiver(Parcel in) {
        mHandler = null;
        IBinder b = in.readStrongBinder();
        mTarget = b instanceof Link ? ((Link) b).mOwner : null;
    }

    public void send(final int resultCode, final Bundle resultData) {
        if (mTarget != null) {
            mTarget.send(resultCode, resultData);
        } else if (mHandler != null) {
            mHandler.post(new Runnable() {
                public void run() {
                    onReceiveResult(resultCode, resultData);
                }
            });
        } else {
            onReceiveResult(resultCode, resultData);
        }
    }

    protected void onReceiveResult(int resultCode, Bundle resultData) {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        synchronized (this) {
            if (mTarget != null) {
                mTarget.writeToParcel(out, flags);
                return;
            }
            if (mLink == null) mLink = new Link(this);
            out.writeStrongBinder(mLink);
        }
    }

    public static final Parcelable.Creator<ResultReceiver> CREATOR = new Parcelable.Creator<ResultReceiver>() {
        public ResultReceiver createFromParcel(Parcel in) {
            return new ResultReceiver(in);
        }

        public ResultReceiver[] newArray(int size) {
            return new ResultReceiver[size];
        }
    };
}
