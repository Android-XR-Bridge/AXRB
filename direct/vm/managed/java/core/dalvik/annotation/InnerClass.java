package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface InnerClass extends Annotation {
    String name();

    int accessFlags();
}
