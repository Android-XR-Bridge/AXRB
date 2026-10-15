package com.axrb.runtime;

import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.CopyOnWriteArrayList;

/** Process-local Android broadcast delivery through the real receiver dispatcher. */
final class HostBroadcasts {
    private static final class Registration {
        final Object receiver;
        final IntentFilter filter;
        Registration(Object receiver, IntentFilter filter) { this.receiver = receiver; this.filter = new IntentFilter(filter); }
    }
    private static final CopyOnWriteArrayList<Registration> registrations = new CopyOnWriteArrayList<>();
    static Intent register(Object[] args) throws Exception {
        Object receiver = null;
        IntentFilter filter = null;
        Class<?> type = Class.forName("android.content.IIntentReceiver");
        for (Object arg : args) {
            if (arg != null && type.isInstance(arg)) receiver = arg;
            if (arg instanceof IntentFilter) filter = (IntentFilter)arg;
        }
        if (filter == null) throw new IllegalArgumentException("Missing receiver filter");
        if (receiver != null) registrations.add(new Registration(receiver, filter));
        return null;
    }
    static void unregister(Object receiver) { registrations.removeIf(r -> r.receiver == receiver); }
    static void send(Intent intent) {
        for (Registration registration : registrations) {
            if (registration.filter.match(intent.getAction(), intent.getType(), intent.getScheme(), intent.getData(), intent.getCategories(), "AXRB") < 0) continue;
            new Handler(Looper.getMainLooper()).post(() -> {
                if (!registrations.contains(registration)) return;
                try { Reflect.call(registration.receiver, "performReceive", new Intent(intent), 0, null, null, false, false, 0); }
                catch (Exception e) { throw new IllegalStateException("Broadcast delivery failed", e); }
            });
        }
    }
}
