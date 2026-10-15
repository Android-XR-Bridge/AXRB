package java.lang.reflect;

public class Modifier {
    public static final int PUBLIC = 0x1;
    public static final int PRIVATE = 0x2;
    public static final int PROTECTED = 0x4;
    public static final int STATIC = 0x8;
    public static final int FINAL = 0x10;
    public static final int SYNCHRONIZED = 0x20;
    public static final int VOLATILE = 0x40;
    public static final int TRANSIENT = 0x80;
    public static final int NATIVE = 0x100;
    public static final int INTERFACE = 0x200;
    public static final int ABSTRACT = 0x400;
    public static final int STRICT = 0x800;

    public static boolean isPublic(int mod) {
        return (mod & PUBLIC) != 0;
    }

    public static boolean isPrivate(int mod) {
        return (mod & PRIVATE) != 0;
    }

    public static boolean isProtected(int mod) {
        return (mod & PROTECTED) != 0;
    }

    public static boolean isStatic(int mod) {
        return (mod & STATIC) != 0;
    }

    public static boolean isFinal(int mod) {
        return (mod & FINAL) != 0;
    }

    public static boolean isSynchronized(int mod) {
        return (mod & SYNCHRONIZED) != 0;
    }

    public static boolean isVolatile(int mod) {
        return (mod & VOLATILE) != 0;
    }

    public static boolean isTransient(int mod) {
        return (mod & TRANSIENT) != 0;
    }

    public static boolean isNative(int mod) {
        return (mod & NATIVE) != 0;
    }

    public static boolean isInterface(int mod) {
        return (mod & INTERFACE) != 0;
    }

    public static boolean isAbstract(int mod) {
        return (mod & ABSTRACT) != 0;
    }

    public static String toString(int mod) {
        StringBuilder sb = new StringBuilder();
        if ((mod & PUBLIC) != 0) sb.append("public ");
        if ((mod & PROTECTED) != 0) sb.append("protected ");
        if ((mod & PRIVATE) != 0) sb.append("private ");
        if ((mod & ABSTRACT) != 0) sb.append("abstract ");
        if ((mod & STATIC) != 0) sb.append("static ");
        if ((mod & FINAL) != 0) sb.append("final ");
        if ((mod & TRANSIENT) != 0) sb.append("transient ");
        if ((mod & VOLATILE) != 0) sb.append("volatile ");
        if ((mod & SYNCHRONIZED) != 0) sb.append("synchronized ");
        if ((mod & NATIVE) != 0) sb.append("native ");
        if ((mod & INTERFACE) != 0) sb.append("interface ");
        int len = sb.length();
        return len > 0 ? sb.substring(0, len - 1) : "";
    }
}
