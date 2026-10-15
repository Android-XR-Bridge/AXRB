package com.axrb.runtime;

import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;
import android.os.Parcel;
import android.view.Surface;
import java.lang.ref.WeakReference;
import java.util.concurrent.ConcurrentHashMap;

/** Routes the AOSP MediaPlayer native boundary to the Windows media backend. */
public final class HostMedia {
    // Keep the backend state until release/finalize. A WeakHashMap can discard
    // the entry before MediaPlayer's finalizer has closed the native decoder.
    private static final ConcurrentHashMap<Long, State> players = new ConcurrentHashMap<>();
    private static final class State {
        volatile long id;
        volatile int session;
        WeakReference<MediaPlayer> owner;
        volatile boolean started, released;
    }
    static native long command(int operation, long id, long a, long b, long c);
    static native long readFrame(long surface, long destination, int width, int height, long previous);
    static native void releaseSurface(long surface);
    private static State state(MediaPlayer player) {
        try {
            State state = players.get((Long)Reflect.field(player, "mNativeContext"));
            if (state == null || state.released) throw new IllegalStateException("MediaPlayer released");
            return state;
        } catch (Exception error) { throw new IllegalStateException(error); }
    }
    public static void setup(MediaPlayer player, Object reference, int session) throws Exception {
        State state = new State();
        state.id = command(0, 0, 0, 0, 0);
        state.session = session > 0 ? session : (int)state.id;
        state.owner = new WeakReference<>(player);
        players.put(state.id, state);
        Reflect.set(player, "mNativeContext", state.id);
    }
    private static void event(State state, int what, int a, int b) {
        if (state.released) return;
        try { Reflect.call(MediaPlayer.class, "postEventFromNative", state.owner, what, a, b, null); }
        catch (Exception error) { throw new IllegalStateException(error); }
    }
    public static long control(MediaPlayer player, int op, long a, long b, long c) throws Exception {
        if (op == 12 && (Long)Reflect.field(player, "mNativeContext") == 0L) return 0;
        State state = state(player);
        if (op == 100) return state.session;
        if (op == 101) { state.session = (int)a; return 0; }
        if (op == 12) {
            state.released = true;
            players.remove(state.id);
            Reflect.set(player, "mNativeContext", 0L);
        }
        if (op == 103) {
            new Thread(() -> {
                long result = command(3, state.id, 0, 0, 0);
                if (result == 0) {
                    event(state, 5, (int)command(13, state.id, 0, 0, 0), (int)command(14, state.id, 0, 0, 0));
                    event(state, 1, 0, 0);
                } else event(state, 100, 1, -1004);
            }, "AXRB media prepare").start();
            return 0;
        }
        if (op == 104) {
            command(12, state.id, 0, 0, 0);
            players.remove(state.id);
            state.id = command(0, 0, 0, 0, 0);
            players.put(state.id, state);
            state.started = false;
            Reflect.set(player, "mNativeContext", state.id);
            return 0;
        }
        long result = command(op, state.id, a, b, c);
        if (result < 0) throw new java.io.IOException("Media operation " + op + " failed");
        if (op == 3) event(state, 5, (int)command(13, state.id, 0, 0, 0), (int)command(14, state.id, 0, 0, 0));
        if (op == 7) event(state, 4, 0, 0);
        if (op == 5 || op == 6) state.started = false;
        if (op == 4 && !state.started) {
            state.started = true;
            Handler handler = new Handler(Looper.getMainLooper());
            handler.postDelayed(new Runnable() {
                public void run() {
                    if (!state.started || state.released || state.owner.get() == null) return;
                    if (command(9, state.id, 0, 0, 0) == 0) { state.started = false; event(state, 2, 0, 0); }
                    else handler.postDelayed(this, 30);
                }
            }, 30);
        }
        return result;
    }
    public static void surface(MediaPlayer player, Surface surface) {
        if (surface != null && !(surface instanceof HostSurface))
            throw new UnsupportedOperationException("Media output requires an AXRB host surface");
        command(2, state(player).id, surface == null ? 0 : ((HostSurface)surface).mediaHandle, 0, 0);
    }
    public static int invoke(MediaPlayer player, Parcel request, Parcel reply) {
        state(player);
        request.setDataPosition(0);
        request.enforceInterface("android.media.IMediaPlayer");
        int operation = request.readInt();
        if (reply != null) {
            reply.writeInt(0);
            if (operation == 1) reply.writeInt(0); // No subtitle or timed-text tracks in the host decoder.
            if (operation == 7) reply.writeInt(-1);
            reply.setDataPosition(0);
        }
        return operation == 1 || operation == 6 || operation == 7 || operation == 8 ? 0 : -38;
    }
}
