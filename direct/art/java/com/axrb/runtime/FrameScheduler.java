package com.axrb.runtime;

import android.os.Handler;
import android.os.Looper;
import java.lang.ref.WeakReference;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/** Supplies real AOSP Choreographer with the host's monotonic frame timeline. */
public final class FrameScheduler {
    private static final long PERIOD = 16_666_667L;
    private static final AtomicLong ids = new AtomicLong();
    private static final ConcurrentHashMap<Long, FrameScheduler> receivers = new ConcurrentHashMap<>();
    private final WeakReference<?> receiver, event;
    private final Handler handler;
    private boolean pending;
    private int frame;

    private FrameScheduler(WeakReference<?> receiver, WeakReference<?> event) {
        this.receiver = receiver;
        this.event = event;
        handler = new Handler(Looper.myLooper());
    }
    public static long create(Object receiver, Object event) {
        long id = ids.incrementAndGet();
        receivers.put(id, new FrameScheduler((WeakReference<?>) receiver, (WeakReference<?>) event));
        return id;
    }
    public static void dispose(long id) { receivers.remove(id); }
    public static void schedule(long id) {
        FrameScheduler scheduler = receivers.get(id);
        if (scheduler == null) return;
        synchronized (scheduler) {
            if (scheduler.pending) return;
            scheduler.pending = true;
        }
        scheduler.handler.postDelayed(() -> {
            synchronized (scheduler) { scheduler.pending = false; }
            Object receiver = scheduler.receiver.get();
            if (receiver == null || !receivers.containsKey(id)) return;
            try {
                long now = System.nanoTime();
                scheduler.update(now);
                Reflect.call(receiver, "dispatchVsync", now, 0L, ++scheduler.frame);
            } catch (Exception error) { throw new RuntimeException("Host vsync dispatch failed", error); }
        }, 16);
    }
    private Object update(long now) throws Exception {
        Object data = event.get();
        if (data == null) return null;
        Reflect.set(data, "frameInterval", PERIOD);
        Reflect.set(data, "frameTimelinesLength", 1);
        Reflect.set(data, "preferredFrameTimelineIndex", 0);
        Object timeline = java.lang.reflect.Array.get(Reflect.field(data, "frameTimelines"), 0);
        Reflect.set(timeline, "vsyncId", (long) frame + 1);
        Reflect.set(timeline, "expectedPresentationTime", now + PERIOD);
        Reflect.set(timeline, "deadline", now + PERIOD);
        return data;
    }
    public static Object latest(long id) throws Exception {
        FrameScheduler scheduler = receivers.get(id);
        return scheduler == null ? null : scheduler.update(System.nanoTime());
    }
    public static void installFinalizer(long finalizer) throws Exception {
        Object registry = Reflect.call(Class.forName("libcore.util.NativeAllocationRegistry"),
                "createMalloced", FrameScheduler.class.getClassLoader(), finalizer);
        Reflect.set(Class.forName("android.view.DisplayEventReceiver"), "sNativeAllocationRegistry", registry);
    }
}
