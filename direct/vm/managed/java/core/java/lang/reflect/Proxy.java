package java.lang.reflect;

import java.util.ArrayList;
import java.util.HashMap;

/**
 * Dynamic proxies. The runtime defines a class "$ProxyN" extending Proxy whose
 * methods (every interface method plus equals/hashCode/toString) forward to the
 * InvocationHandler in `h`.
 */
public class Proxy implements java.io.Serializable {
    private static final HashMap<String, Class<?>> sCache = new HashMap<String, Class<?>>();
    private static final ArrayList<Class<?>> sProxyClasses = new ArrayList<Class<?>>();

    protected InvocationHandler h;

    protected Proxy(InvocationHandler h) {
        this.h = h;
    }

    private static String key(ClassLoader loader, Class<?>[] interfaces) {
        StringBuilder sb = new StringBuilder();
        sb.append(System.identityHashCode(loader));
        for (Class<?> c : interfaces) sb.append(';').append(c.getName()).append('@').append(System.identityHashCode(c));
        return sb.toString();
    }

    public static Class<?> getProxyClass(ClassLoader loader, Class<?>... interfaces) {
        if (interfaces == null) throw new NullPointerException("interfaces == null");
        String key = key(loader, interfaces);
        synchronized (sCache) {
            Class<?> c = sCache.get(key);
            if (c == null) {
                c = generateProxy(interfaces.clone(), loader);
                sCache.put(key, c);
                sProxyClasses.add(c);
            }
            return c;
        }
    }

    public static Object newProxyInstance(ClassLoader loader, Class<?>[] interfaces, InvocationHandler h) {
        if (h == null) throw new NullPointerException("h == null");
        return newInstance0(getProxyClass(loader, interfaces), h);
    }

    public static boolean isProxyClass(Class<?> cl) {
        synchronized (sCache) {
            return sProxyClasses.contains(cl);
        }
    }

    public static InvocationHandler getInvocationHandler(Object proxy) {
        if (!(proxy instanceof Proxy) || !isProxyClass(proxy.getClass())) {
            throw new IllegalArgumentException("not a proxy instance");
        }
        return ((Proxy) proxy).h;
    }

    private static native Class<?> generateProxy(Class<?>[] interfaces, ClassLoader loader);

    private static native Object newInstance0(Class<?> proxyClass, InvocationHandler h);
}
