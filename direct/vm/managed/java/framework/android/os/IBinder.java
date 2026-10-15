package android.os;

/**
 * A binder object. The runner has no IPC; binders are in-process tokens (for
 * example window tokens) and Binder subclasses called within the process.
 */
public interface IBinder {
    int FIRST_CALL_TRANSACTION = 0x00000001;
    int LAST_CALL_TRANSACTION = 0x00ffffff;
    int PING_TRANSACTION = ('_' << 24) | ('P' << 16) | ('N' << 8) | 'G';
    int DUMP_TRANSACTION = ('_' << 24) | ('D' << 16) | ('M' << 8) | 'P';
    int INTERFACE_TRANSACTION = ('_' << 24) | ('N' << 16) | ('T' << 8) | 'F';
    int FLAG_ONEWAY = 0x00000001;

    String getInterfaceDescriptor() throws RemoteException;

    boolean pingBinder();

    boolean isBinderAlive();

    IInterface queryLocalInterface(String descriptor);

    boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException;

    public interface DeathRecipient {
        void binderDied();
    }

    void linkToDeath(DeathRecipient recipient, int flags) throws RemoteException;

    boolean unlinkToDeath(DeathRecipient recipient, int flags);
}
