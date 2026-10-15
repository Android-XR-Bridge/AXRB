package android.os;

import java.util.ArrayList;

/**
 * Messages ordered by delivery time. Blocking waits happen in the host
 * (nativePollOnce), which also pumps the host window and delivers input, the
 * way Android's native Looper services its file descriptors.
 */
public final class MessageQueue {
    private final boolean quitAllowed;
    private long ptr;
    private Message messages;
    private boolean quitting;
    private boolean blocked;
    private final ArrayList<IdleHandler> idleHandlers = new ArrayList<IdleHandler>();

    public interface IdleHandler {
        boolean queueIdle();
    }

    MessageQueue(boolean quitAllowed) {
        this.quitAllowed = quitAllowed;
        ptr = nativeInit();
    }

    private static native long nativeInit();

    private static native void nativeDestroy(long ptr);

    /** Waits up to timeoutMillis (-1: until woken). May dispatch host input. */
    private static native void nativePollOnce(long ptr, int timeoutMillis);

    private static native void nativeWake(long ptr);

    public void addIdleHandler(IdleHandler handler) {
        if (handler == null) throw new NullPointerException("Can't add a null IdleHandler");
        synchronized (this) {
            idleHandlers.add(handler);
        }
    }

    public void removeIdleHandler(IdleHandler handler) {
        synchronized (this) {
            idleHandlers.remove(handler);
        }
    }

    public boolean isIdle() {
        synchronized (this) {
            return messages == null || SystemClock.uptimeMillis() < messages.when;
        }
    }

    Message next() {
        int timeout = 0;
        for (;;) {
            if (ptr == 0) return null;
            nativePollOnce(ptr, timeout);
            IdleHandler[] idlers = null;
            synchronized (this) {
                final long now = SystemClock.uptimeMillis();
                Message m = messages;
                if (m != null && now >= m.when) {
                    blocked = false;
                    messages = m.next;
                    m.next = null;
                    return m;
                }
                if (quitting) {
                    dispose();
                    return null;
                }
                timeout = m == null ? -1 : (int) Math.min(m.when - now, Integer.MAX_VALUE);
                if (!idleHandlers.isEmpty()) idlers = idleHandlers.toArray(new IdleHandler[0]);
                blocked = idlers == null;
            }
            if (idlers != null) {
                for (IdleHandler h : idlers) {
                    boolean keep = false;
                    try {
                        keep = h.queueIdle();
                    } catch (Throwable t) {
                        android.util.Log.wtf("MessageQueue", "IdleHandler threw exception", t);
                    }
                    if (!keep) {
                        synchronized (this) {
                            idleHandlers.remove(h);
                        }
                    }
                }
                // Idle handlers may have posted work: look again without waiting.
                timeout = 0;
                synchronized (this) {
                    blocked = false;
                }
            }
        }
    }

    void quit(boolean safe) {
        if (!quitAllowed) throw new IllegalStateException("Main thread not allowed to quit.");
        synchronized (this) {
            if (quitting) return;
            quitting = true;
            if (safe) {
                removeFutureMessages(SystemClock.uptimeMillis());
            } else {
                messages = null;
            }
            nativeWake(ptr);
        }
    }

    /** Ends next() even on the main queue (session end; see Looper). */
    void shutdown() {
        synchronized (this) {
            quitting = true;
            messages = null;
            nativeWake(ptr);
        }
    }

    private void removeFutureMessages(long now) {
        Message prev = null;
        for (Message m = messages; m != null; m = m.next) {
            if (m.when > now) {
                if (prev == null) {
                    messages = null;
                } else {
                    prev.next = null;
                }
                return;
            }
            prev = m;
        }
    }

    private void dispose() {
        if (ptr != 0) {
            nativeDestroy(ptr);
            ptr = 0;
        }
    }

    boolean enqueueMessage(Message msg, long when) {
        if (msg.target == null) throw new IllegalArgumentException("Message must have a target.");
        synchronized (this) {
            if (quitting) {
                android.util.Log.w("MessageQueue", msg.target + " sending message to a Handler on a dead thread");
                msg.recycle();
                return false;
            }
            msg.when = when;
            Message p = messages;
            if (p == null || when == 0 || when < p.when) {
                msg.next = p;
                messages = msg;
            } else {
                Message prev;
                for (;;) {
                    prev = p;
                    p = p.next;
                    if (p == null || when < p.when) break;
                }
                msg.next = p;
                prev.next = msg;
            }
            nativeWake(ptr);
        }
        return true;
    }

    boolean hasMessages(Handler h, int what, Object object) {
        synchronized (this) {
            for (Message m = messages; m != null; m = m.next) {
                if (m.target == h && m.what == what && (object == null || m.obj == object)) return true;
            }
            return false;
        }
    }

    boolean hasCallbacks(Handler h, Runnable r) {
        synchronized (this) {
            for (Message m = messages; m != null; m = m.next) {
                if (m.target == h && m.callback == r) return true;
            }
            return false;
        }
    }

    void removeMessages(Handler h, int what, Object object) {
        removeIf(h, what, null, object, true);
    }

    void removeCallbacks(Handler h, Runnable r, Object object) {
        if (r == null) return;
        removeIf(h, 0, r, object, false);
    }

    void removeCallbacksAndMessages(Handler h, Object object) {
        synchronized (this) {
            Message prev = null;
            Message m = messages;
            while (m != null) {
                Message n = m.next;
                if (m.target == h && (object == null || m.obj == object)) {
                    if (prev == null) messages = n; else prev.next = n;
                    m.recycle();
                } else {
                    prev = m;
                }
                m = n;
            }
        }
    }

    private void removeIf(Handler h, int what, Runnable r, Object object, boolean byWhat) {
        synchronized (this) {
            Message prev = null;
            Message m = messages;
            while (m != null) {
                Message n = m.next;
                boolean match = m.target == h && (object == null || m.obj == object)
                        && (byWhat ? m.what == what : m.callback == r);
                if (match) {
                    if (prev == null) messages = n; else prev.next = n;
                    m.recycle();
                } else {
                    prev = m;
                }
                m = n;
            }
        }
    }
}
