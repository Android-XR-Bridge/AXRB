package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface EnclosingClass extends Annotation {
    Class<?> value();
}
