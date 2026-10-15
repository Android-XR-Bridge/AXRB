package android.content;

public final class ComponentName implements Cloneable, Comparable<ComponentName> {
    private final String mPackage;
    private final String mClass;

    public ComponentName(String pkg, String cls) {
        if (pkg == null) throw new NullPointerException("package name is null");
        if (cls == null) throw new NullPointerException("class name is null");
        mPackage = pkg;
        mClass = cls;
    }

    public ComponentName(Context pkg, String cls) {
        this(pkg.getPackageName(), cls);
    }

    public ComponentName(Context pkg, Class<?> cls) {
        this(pkg.getPackageName(), cls.getName());
    }

    public String getPackageName() {
        return mPackage;
    }

    public String getClassName() {
        return mClass;
    }

    public String getShortClassName() {
        if (mClass.startsWith(mPackage)) {
            int pn = mPackage.length();
            if (mClass.length() > pn && mClass.charAt(pn) == '.') return mClass.substring(pn);
        }
        return mClass;
    }

    public String flattenToString() {
        return mPackage + "/" + mClass;
    }

    public String flattenToShortString() {
        return mPackage + "/" + getShortClassName();
    }

    public String toShortString() {
        return "{" + mPackage + "/" + mClass + "}";
    }

    public static ComponentName unflattenFromString(String str) {
        int sep = str.indexOf('/');
        if (sep < 0 || sep + 1 >= str.length()) return null;
        String pkg = str.substring(0, sep);
        String cls = str.substring(sep + 1);
        if (cls.length() > 0 && cls.charAt(0) == '.') cls = pkg + cls;
        return new ComponentName(pkg, cls);
    }

    @Override
    public ComponentName clone() {
        return new ComponentName(mPackage, mClass);
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof ComponentName)) return false;
        ComponentName other = (ComponentName) obj;
        return mPackage.equals(other.mPackage) && mClass.equals(other.mClass);
    }

    @Override
    public int hashCode() {
        return mPackage.hashCode() + mClass.hashCode();
    }

    @Override
    public int compareTo(ComponentName that) {
        int v = mPackage.compareTo(that.mPackage);
        return v != 0 ? v : mClass.compareTo(that.mClass);
    }

    @Override
    public String toString() {
        return "ComponentInfo{" + mPackage + "/" + mClass + "}";
    }
}
