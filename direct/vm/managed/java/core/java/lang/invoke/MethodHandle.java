package java.lang.invoke;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * Method handles over reflected methods (what libraries reach through
 * MethodHandles.Lookup.unreflect*: calling default methods from proxies).
 * Lambdas never get here; d8 desugars them.
 */
public class MethodHandle {
    private final Method method;
    private final Object bound;
    private final boolean hasBound;
    private final boolean special;

    MethodHandle(Method method, boolean special) {
        this(method, special, null, false);
    }

    private MethodHandle(Method method, boolean special, Object bound, boolean hasBound) {
        this.method = method;
        this.special = special;
        this.bound = bound;
        this.hasBound = hasBound;
    }

    public MethodType type() {
        throw MethodHandles.unsupported();
    }

    public MethodHandle bindTo(Object x) {
        if (hasBound || method == null || Modifier.isStatic(method.getModifiers())) {
            throw new IllegalArgumentException("no leading reference parameter");
        }
        return new MethodHandle(method, special, x, true);
    }

    public Object invokeWithArguments(Object... arguments) throws Throwable {
        if (method == null) throw MethodHandles.unsupported();
        if (arguments == null) arguments = new Object[0];
        Object receiver = null;
        Object[] args = arguments;
        if (!Modifier.isStatic(method.getModifiers())) {
            if (hasBound) {
                receiver = bound;
            } else {
                if (arguments.length == 0) throw new IllegalArgumentException("missing receiver");
                receiver = arguments[0];
                args = new Object[arguments.length - 1];
                System.arraycopy(arguments, 1, args, 0, args.length);
            }
        }
        try {
            return special ? method.invokeNonVirtual(receiver, args) : method.invoke(receiver, args);
        } catch (InvocationTargetException e) {
            throw e.getCause();
        }
    }

    public Object invokeWithArguments(java.util.List<?> arguments) throws Throwable {
        return invokeWithArguments(arguments.toArray());
    }

    public final native Object invokeExact(Object... args) throws Throwable;

    public final native Object invoke(Object... args) throws Throwable;
}
