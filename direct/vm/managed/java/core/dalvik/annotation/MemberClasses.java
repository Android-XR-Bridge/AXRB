package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface MemberClasses extends Annotation {
    Class<?>[] value();
}
