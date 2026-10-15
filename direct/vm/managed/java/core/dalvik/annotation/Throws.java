package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface Throws extends Annotation {
    Class<?>[] value();
}
