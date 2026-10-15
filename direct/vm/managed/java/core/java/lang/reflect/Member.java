package java.lang.reflect;

public interface Member {
    int PUBLIC = 0;
    int DECLARED = 1;

    Class<?> getDeclaringClass();

    String getName();

    int getModifiers();

    boolean isSynthetic();
}
