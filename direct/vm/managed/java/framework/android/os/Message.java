package android.os;

public final class Message implements Parcelable {
    public int what;
    public int arg1;
    public int arg2;
    public Object obj;
    public Messenger replyTo;

    long when;
    Handler target;
    Runnable callback;
    Message next;
    private Bundle data;

    private static final Object POOL_LOCK = new Object();
    private static Message pool;
    private static int poolSize;

    public Message() {}

    public static final Parcelable.Creator<Message> CREATOR = new Parcelable.Creator<Message>() {
        public Message createFromParcel(Parcel source) {
            Message msg = Message.obtain();
            msg.what = source.readInt();
            msg.arg1 = source.readInt();
            msg.arg2 = source.readInt();
            if (source.readInt() != 0) msg.obj = source.readParcelable(Message.class.getClassLoader());
            msg.when = source.readLong();
            msg.data = source.readBundle();
            return msg;
        }

        public Message[] newArray(int size) {
            return new Message[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        if (callback != null) throw new RuntimeException("Can't marshal callbacks across processes.");
        dest.writeInt(what);
        dest.writeInt(arg1);
        dest.writeInt(arg2);
        if (obj != null) {
            if (!(obj instanceof Parcelable)) {
                throw new RuntimeException("Can't marshal non-Parcelable objects across processes.");
            }
            dest.writeInt(1);
            dest.writeParcelable((Parcelable) obj, flags);
        } else {
            dest.writeInt(0);
        }
        dest.writeLong(when);
        dest.writeBundle(data);
    }

    public static Message obtain() {
        synchronized (POOL_LOCK) {
            if (pool != null) {
                Message m = pool;
                pool = m.next;
                m.next = null;
                poolSize--;
                return m;
            }
        }
        return new Message();
    }

    public static Message obtain(Handler h) {
        Message m = obtain();
        m.target = h;
        return m;
    }

    public static Message obtain(Handler h, Runnable callback) {
        Message m = obtain(h);
        m.callback = callback;
        return m;
    }

    public static Message obtain(Handler h, int what) {
        Message m = obtain(h);
        m.what = what;
        return m;
    }

    public static Message obtain(Handler h, int what, Object obj) {
        Message m = obtain(h, what);
        m.obj = obj;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2) {
        Message m = obtain(h, what);
        m.arg1 = arg1;
        m.arg2 = arg2;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2, Object obj) {
        Message m = obtain(h, what, arg1, arg2);
        m.obj = obj;
        return m;
    }

    public void recycle() {
        what = arg1 = arg2 = 0;
        obj = null;
        when = 0;
        target = null;
        callback = null;
        data = null;
        synchronized (POOL_LOCK) {
            if (poolSize < 50) {
                next = pool;
                pool = this;
                poolSize++;
            }
        }
    }

    public long getWhen() {
        return when;
    }

    public Handler getTarget() {
        return target;
    }

    public void setTarget(Handler target) {
        this.target = target;
    }

    public Runnable getCallback() {
        return callback;
    }

    public Bundle getData() {
        if (data == null) data = new Bundle();
        return data;
    }

    public Bundle peekData() {
        return data;
    }

    public void setData(Bundle data) {
        this.data = data;
    }

    public void sendToTarget() {
        target.sendMessage(this);
    }

    @Override
    public String toString() {
        return "{ when=" + (when - SystemClock.uptimeMillis()) + "ms what=" + what + " target="
                + (target == null ? "null" : target.getClass().getName()) + " }";
    }
}
