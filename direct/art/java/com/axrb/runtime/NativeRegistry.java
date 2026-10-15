package com.axrb.runtime;

import android.os.*;
import java.util.*;

/** The native libbinder and Java service managers share one session registry. */
final class NativeRegistry extends Binder {
    private static final String DESCRIPTOR = "android.os.IServiceManager";
    private final Map<String, IBinder> services;
    private final Map<Integer, String> transactions = new HashMap<>();
    private static native boolean installNative(IBinder registry);

    NativeRegistry(Map<String, IBinder> services) throws Exception {
        this.services = services;
        for (java.lang.reflect.Field field : Class.forName(DESCRIPTOR + "$Stub").getDeclaredFields())
            if (field.getName().startsWith("TRANSACTION_")) {
                field.setAccessible(true);
                transactions.put(field.getInt(null), field.getName().substring(12));
            }
        if (!installNative(this)) throw new IllegalStateException("Cannot install native Binder service registry");
    }

    @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (code == INTERFACE_TRANSACTION) { reply.writeString(DESCRIPTOR); return true; }
        String method = transactions.get(code);
        if (method == null) return super.onTransact(code, data, reply, flags);
        data.enforceInterface(DESCRIPTOR);
        try {
            switch (method) {
                case "getService": case "checkService": case "getService2": case "checkService2": {
                    String name = data.readString();
                    IBinder binder = services.get(name);
                    System.out.println("AXRB_NATIVE_SERVICE " + method + " " + name + " present=" + (binder != null));
                    reply.writeNoException();
                    if (method.endsWith("2")) {
                        Object metadata = Reflect.create("android.os.ServiceWithMetadata");
                        Reflect.set(metadata, "service", binder);
                        Parcelable result = (Parcelable) Reflect.call(Class.forName("android.os.Service"), "serviceWithMetadata", metadata);
                        reply.writeTypedObject(result, Parcelable.PARCELABLE_WRITE_RETURN_VALUE);
                    } else reply.writeStrongBinder(binder);
                    return true;
                }
                case "isDeclared": reply.writeNoException(); reply.writeBoolean(services.containsKey(data.readString())); return true;
                case "listServices": reply.writeNoException(); reply.writeStringArray(services.keySet().toArray(new String[0])); return true;
                case "getDeclaredInstances": reply.writeNoException(); reply.writeStringArray(new String[0]); return true;
                default: System.err.println("AXRB_NATIVE_REGISTRY_UNIMPLEMENTED " + method); return false;
            }
        } catch (Exception error) { reply.writeException(error); return true; }
    }
}
