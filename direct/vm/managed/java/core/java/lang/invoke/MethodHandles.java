package java.lang.invoke;

import java.lang.reflect.Method;

public class MethodHandles {
    private MethodHandles() {}

    static UnsupportedOperationException unsupported() {
        return new UnsupportedOperationException("this java.lang.invoke operation is not supported by this runtime");
    }

    public static Lookup lookup() {
        return new Lookup(Object.class, Lookup.ALL_MODES);
    }

    public static Lookup publicLookup() {
        return new Lookup(Object.class, Lookup.PUBLIC);
    }

    public static Lookup privateLookupIn(Class<?> targetClass, Lookup lookup) {
        return new Lookup(targetClass, Lookup.ALL_MODES);
    }

    public static final class Lookup {
        public static final int PUBLIC = 0x01;
        public static final int PRIVATE = 0x02;
        public static final int PROTECTED = 0x04;
        public static final int PACKAGE = 0x08;
        static final int ALL_MODES = PUBLIC | PRIVATE | PROTECTED | PACKAGE;

        private final Class<?> lookupClass;
        private final int allowedModes;

        /** Libraries reach this reflectively to build a full-privilege lookup. */
        private Lookup(Class<?> lookupClass, int allowedModes) {
            this.lookupClass = lookupClass;
            this.allowedModes = allowedModes;
        }

        private Lookup(Class<?> lookupClass) {
            this(lookupClass, ALL_MODES);
        }

        public Class<?> lookupClass() {
            return lookupClass;
        }

        public int lookupModes() {
            return allowedModes;
        }

        public Lookup in(Class<?> requestedLookupClass) {
            return new Lookup(requestedLookupClass, allowedModes);
        }

        public MethodHandle unreflect(Method m) {
            return new MethodHandle(m, false);
        }

        public MethodHandle unreflectSpecial(Method m, Class<?> specialCaller) {
            return new MethodHandle(m, true);
        }

        public MethodHandle findVirtual(Class<?> refc, String name, MethodType type) {
            throw unsupported();
        }

        public MethodHandle findStatic(Class<?> refc, String name, MethodType type) {
            throw unsupported();
        }

        public MethodHandle findSpecial(Class<?> refc, String name, MethodType type, Class<?> specialCaller) {
            throw unsupported();
        }
    }
}
