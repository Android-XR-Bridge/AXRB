package java.lang.reflect;

/**
 * Access checks for reflection are not enforced by this runtime yet: setAccessible
 * is recorded but invoke/get/set behave as if it were true. Documented in
 * docs/managed-runtime.md as a known gap.
 */
public class AccessibleObject {
    boolean accessible;

    protected AccessibleObject() {}

    public void setAccessible(boolean flag) {
        accessible = flag;
    }

    public boolean isAccessible() {
        return accessible;
    }

    /** Runtime annotations are not retained by this runtime: none are present. */
    public <T extends java.lang.annotation.Annotation> T getAnnotation(Class<T> annotationClass) {
        if (annotationClass == null) throw new NullPointerException();
        return null;
    }

    public boolean isAnnotationPresent(Class<? extends java.lang.annotation.Annotation> annotationClass) {
        return getAnnotation(annotationClass) != null;
    }

    public java.lang.annotation.Annotation[] getAnnotations() {
        return new java.lang.annotation.Annotation[0];
    }

    public java.lang.annotation.Annotation[] getDeclaredAnnotations() {
        return new java.lang.annotation.Annotation[0];
    }
}
