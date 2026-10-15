package dalvik.annotation;

import java.lang.annotation.Annotation;

public interface Signature extends Annotation {
    String[] value();
}
