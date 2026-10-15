package android.os;

/** In-process only: a Messenger wraps a Handler, there are no remote binders. */
public final class Messenger implements Parcelable {
    private final Handler mTarget;

    public Messenger(Handler target) {
        mTarget = target;
    }

    public Messenger(IBinder target) {
        throw new UnsupportedOperationException("remote Messengers are not supported in this runtime");
    }

    public void send(Message message) throws RemoteException {
        mTarget.sendMessage(message);
    }

    public IBinder getBinder() {
        return null;
    }

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(Parcel out, int flags) {
        throw new UnsupportedOperationException();
    }

    public boolean equals(Object other) {
        return other instanceof Messenger && ((Messenger) other).mTarget == mTarget;
    }

    public int hashCode() {
        return mTarget.hashCode();
    }
}
