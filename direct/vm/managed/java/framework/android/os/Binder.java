package android.os;

/** An in-process binder: transact() calls onTransact() directly. */
public class Binder implements IBinder {
    private IInterface mOwner;
    private String mDescriptor;

    public Binder() {}

    public Binder(String descriptor) {
        mDescriptor = descriptor;
    }

    public static final int getCallingPid() {
        return Process.myPid();
    }

    public static final int getCallingUid() {
        return Process.myUid();
    }

    public static final UserHandle getCallingUserHandle() {
        return Process.myUserHandle();
    }

    public static final long clearCallingIdentity() {
        return 0;
    }

    public static final void restoreCallingIdentity(long token) {}

    public static final void flushPendingCommands() {}

    public void attachInterface(IInterface owner, String descriptor) {
        mOwner = owner;
        mDescriptor = descriptor;
    }

    @Override
    public String getInterfaceDescriptor() {
        return mDescriptor;
    }

    @Override
    public boolean pingBinder() {
        return true;
    }

    @Override
    public boolean isBinderAlive() {
        return true;
    }

    @Override
    public IInterface queryLocalInterface(String descriptor) {
        if (mDescriptor != null && mDescriptor.equals(descriptor)) return mOwner;
        return null;
    }

    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            reply.writeString(getInterfaceDescriptor());
            return true;
        }
        return false;
    }

    @Override
    public final boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (data != null) data.setDataPosition(0);
        boolean r = onTransact(code, data, reply, flags);
        if (reply != null) reply.setDataPosition(0);
        return r;
    }

    @Override
    public void linkToDeath(DeathRecipient recipient, int flags) {}

    @Override
    public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return true;
    }
}
